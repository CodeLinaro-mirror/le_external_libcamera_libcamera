/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * Process object
 */

#include "libcamera/internal/process.h"

#include <algorithm>
#include <dirent.h>
#include <fcntl.h>
#include <list>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <linux/sched.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <libcamera/base/event_notifier.h>
#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

/**
 * \file process.h
 * \brief Process object
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(Process)

/**
 * \class Process
 * \brief Process object
 *
 * The Process class models a process, and simplifies spawning new processes
 * and monitoring the exiting of a process.
 */

/**
 * \enum Process::ExitStatus
 * \brief Exit status of process
 * \var Process::NotExited
 * The process hasn't exited yet
 * \var Process::NormalExit
 * The process exited normally, either via exit() or returning from main
 * \var Process::SignalExit
 * The process was terminated by a signal (this includes crashing)
 */

Process::Process()
	: pid_(-1), running_(false), exitStatus_(NotExited), exitCode_(0)
{
}

Process::~Process()
{
	kill();
	/* \todo wait for child process to exit */
}

namespace {

void closeAllFdsExcept(Span<const int> fds)
{
	std::vector<int> v(fds.begin(), fds.end());
	sort(v.begin(), v.end());

	DIR *dir = opendir("/proc/self/fd");
	if (!dir)
		return;

	int dfd = dirfd(dir);

	struct dirent *ent;
	while ((ent = readdir(dir)) != nullptr) {
		char *endp;
		int fd = strtoul(ent->d_name, &endp, 10);
		if (*endp)
			continue;

		if (fd >= 0 && fd != dfd &&
		    !std::binary_search(v.begin(), v.end(), fd))
			close(fd);
	}

	closedir(dir);
}

} /* namespace */

/**
 * \brief Fork and exec a process, and close fds
 * \param[in] path Path to executable
 * \param[in] args Arguments to pass to executable (optional)
 * \param[in] fds Vector of file descriptors to keep open (optional)
 *
 * Fork a process, and exec the executable specified by path. Prior to
 * exec'ing, but after forking, all file descriptors except for those
 * specified in fds will be closed.
 *
 * All indexes of args will be incremented by 1 before being fed to exec(),
 * so args[0] should not need to be equal to path.
 *
 * \return Zero on successful fork, exec, and closing the file descriptors,
 * or a negative error code otherwise
 */
int Process::start(const std::string &path,
		   Span<const std::string> args,
		   Span<const int> fds)
{
	if (running_)
		return 0;

	int pidfd;
	::clone_args cargs = {};

	cargs.flags = CLONE_CLEAR_SIGHAND | CLONE_PIDFD | CLONE_NEWUSER | CLONE_NEWNET;
	cargs.pidfd = reinterpret_cast<uintptr_t>(&pidfd);
	cargs.exit_signal = SIGCHLD;

	long childPid = ::syscall(SYS_clone3, &cargs, sizeof(cargs));
	if (childPid < 0) {
		int ret = -errno;
		LOG(Process, Error) << "Failed to fork: " << strerror(-ret);
		return ret;
	}

	if (childPid) {
		running_ = true;
		pid_ = childPid;
		pidfd_ = UniqueFD(pidfd);
		pidfdNotify_ = std::make_unique<EventNotifier>(pidfd_.get(), EventNotifier::Type::Read);

		pidfdNotify_->activated.connect(this, [this] {
			::siginfo_t info;

			running_ = false;

			if (::waitid(P_PIDFD, pidfd_.get(), &info, WNOHANG | WEXITED) >= 0) {
				ASSERT(info.si_signo == SIGCHLD);
				ASSERT(info.si_pid == pid_);

				LOG(Process, Debug)
					<< this << "[" << pid_ << ":" << pidfd_.get() << "] "
					"code:" << info.si_code << " status:" << info.si_status;

				exitStatus_ = info.si_code == CLD_EXITED ? Process::NormalExit : Process::SignalExit;
				exitCode_ = info.si_code == CLD_EXITED ? info.si_status : -1;

				finished.emit(exitStatus_, exitCode_);
			} else {
				int err = errno;
				LOG(Process, Warning)
					<< this << "[" << pid_ << ":" << pidfd_.get() << "] "
					"waitid() failed: " << strerror(err);
			}

			pid_ = -1;
			pidfd_.reset();
			pidfdNotify_.release();
		});
	} else {
		closeAllFdsExcept(fds);

		const char *file = utils::secure_getenv("LIBCAMERA_LOG_FILE");
		if (file && strcmp(file, "syslog"))
			unsetenv("LIBCAMERA_LOG_FILE");

		const size_t len = args.size();
		const char **argv = new const char *[len + 2];
		argv[0] = path.c_str();
		for (size_t i = 0; i < len; i++)
			argv[i + 1] = args[i].c_str();
		argv[len + 1] = nullptr;

		execv(path.c_str(), const_cast<char * const *>(argv));

		exit(EXIT_FAILURE);
	}

	return 0;
}

/**
 * \fn Process::exitStatus()
 * \brief Retrieve the exit status of the process
 *
 * Return the exit status of the process, that is, whether the process
 * has exited via exit() or returning from main, or if the process was
 * terminated by a signal.
 *
 * \sa ExitStatus
 *
 * \return The process exit status
 */

/**
 * \fn Process::exitCode()
 * \brief Retrieve the exit code of the process
 *
 * This function is only valid if exitStatus() returned NormalExit.
 *
 * \return Exit code
 */

/**
 * \var Process::finished
 *
 * Signal that is emitted when the process is confirmed to have terminated.
 */

/**
 * \brief Kill the process
 *
 * Sends SIGKILL to the process.
 */
void Process::kill()
{
	if (pidfd_.isValid())
		::syscall(SYS_pidfd_send_signal, pidfd_.get(), SIGKILL, nullptr, 0);
}

} /* namespace libcamera */
