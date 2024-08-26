/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Google Inc.
 *
 * task_scheduler.cpp - A task scheduler
 */

#include "libcamera/internal/task_scheduler.h"

#include <libcamera/base/log.h>

/**
 * \internal
 * \file task_scheduler.h
 * \brief Task and (Categorized)Scheduler that run tasks based on dependencies
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(Task)

/**
 * \class Task
 * \brief A set of operations to execute as an individual Task
 *
 * The Task class contains a set of operations to be executed. It may have
 * dependencies on other tasks, and should only be executed by the
 * Scheduler it belongs to when all of its dependency tasks are executed.
 *
 * Upon its end of execution, it should notify its Scheduler by invoking
 * Scheduler::taskDone.
 */

/**
 * \brief Construct a Task instance
 * \param[in] scheduler The Scheduler this task belongs to
 * \param[in] id The ID of this task, only being used in log
 */
Task::Task(Scheduler *scheduler, const std::string &id)
	: scheduler_(scheduler), id_(id)
{
}

/**
 * \brief Notify Scheduler the task has completed
 *
 * It's only called when all operations in this task is done, and it notifies
 * the Scheduler so that the dependencies of its succedents can be resolved.
 *
 * Some derived classes might override this function to do some cleanup at the
 * end of the Task. However, notifying the Scheduler should still be done.
 */
void Task::notifyDone()
{
	scheduler_->invokeMethod(&Scheduler::taskDone, ConnectionTypeQueued, this);
}

/**
 * \fn Task::run()
 * \brief Run the operations defined by the derived class
 *
 * This function runs the operations that should be done, or wait for some
 * callbacks / events, like ISP processing frames, or IPA returning metadata.
 */

/**
 * \fn Task::id()
 * \brief The ID of the Task
 */

/**
 * \fn Task::isRunning()
 * \brief The state if the task has already been running
 *
 * It's mostly used after the user calls Scheduler::groupTasks, and needs to
 * decide if it should use or depend on a Task based on its state.
 */

/**
 * \var Task::scheduler_
 * \brief The Scheduler this Task belongs to
 */

/**
 * \var Task::id_
 * \brief The id of this Task
 */

size_t Task::removeDependency(Task *task)
{
	precedents_.remove(task);
	return precedents_.size();
}

void Task::depend(Task *task)
{
	precedents_.emplace_back(task);
	task->succedents_.emplace_back(this);
}

void Task::launch()
{
	ASSERT(precedents_.empty());

	running_ = true;
	launchTime_ = std::chrono::steady_clock::now();

	auto *method = new BoundMethodMember{
		this, scheduler_, &Task::run, ConnectionTypeQueued
	};

	method->activate();
}

/**
 * \class Scheduler
 * \brief Scheduler to run Tasks based on their dependencies
 *
 * This is the very basic implementation of Scheduler that runs Tasks when
 * they don't have any dependency left. It doesn't have priorities among Tasks.
 */

/**
 * \brief Adds a dependency from \a precedent to \a task
 * \param[in] precedent The precedent Task that is a dependency
 * \param[in] task The Task that needs to set a dependency
 */
void Scheduler::precede(Task *precedent, Task *task)
{
	ASSERT(task && precedent);
	task->depend(precedent);
}

Scheduler::Scheduler() = default;

/**
 * \brief Launch all Tasks that don't have any dependency
 *
 * This is mostly called after some Tasks are queued into the Scheduler.
 */
void Scheduler::schedule()
{
	for (auto it = pendingTasks_.begin(); it != pendingTasks_.end();) {
		auto *task = *it;
		if (!task->precedents_.empty()) {
			it++;
			continue;
		}

		runningTasks_.emplace(task);
		it = pendingTasks_.erase(it);

		task->launch();
	}
}

/**
 * \brief Log all non-finished Tasks, grouped by the group ids
 */
void Scheduler::log()
{
	std::stringstream ss;
	for (auto &[group, name] : groupNames_)
		ss << name << "[" << groupTasks_[group].size() << "] ";

	LOG(Task, Info) << ss.str();
}

/**
 * \brief Queues \a task into the Scheduler, which takes the ownership of the
 * Task
 * \param[in] task The task being queued and will be executed
 * \param[in] group The group ID that the Task belongs to
 *
 * Note that \a task is passed as a raw pointer, to make the developer create
 * and pass Task pointers more easily without holding both raw pointers and
 * unique pointers. Scheduler will then construct a std::unique_ptr to take the
 * ownership.
 *
 * The task will then be held as a pending task. The user of Scheduler should
 * call Scheduler::schedule() to make new tasks without dependencies start to
 * be executed. A Task with dependencies will be executed when its last
 * precedent Task is done.
 */
void Scheduler::queueTask(Task *task, int32_t group)
{
	/* \todo: Detect cyclic dependency */
	tasksHolder_.emplace(task, std::unique_ptr<Task>(task));

	pendingTasks_.emplace(task);
	groupTasks_[group].emplace_back(task);
}

/**
 * \brief Sets a dependency from the \a step th latest Task in group \a group
 * to \a task
 * \param[in] group The group ID
 * \param[in] step The number of Tasks to trace back, 0 being the latest one
 * \param[in] task The Task that needs to add a dependency
 *
 * If the number of Tasks in group \a group is less than or equals to \a step,
 * such a dependent Task doesn't exist, and no dependency will be added.
 */
void Scheduler::succeedPrevTaskByStep(int32_t group, size_t step, Task *task)
{
	ASSERT(task);

	auto &tasks = groupTasks_[group];
	if (tasks.size() <= step)
		return;

	auto iter = tasks.rbegin();
	for (size_t i = 0; i < step; i++)
		iter++;

	precede(*iter, task);
}

/**
 * \brief Return a list of unfinished Tasks that belong to the group ID
 * \param[in] group The group ID
 */
std::list<Task *> &Scheduler::groupTasks(int32_t group)
{
	return groupTasks_[group];
}

/**
 * \var Scheduler::groupNames_
 * \brief The map from group ID to group name
 *
 * Should be set by the Scheduler's derived class
 */

void Scheduler::removeFromGroupTasks(Task *task)
{
	for (auto &[group, tasks] : groupTasks_)
		tasks.remove(task);
}

void Scheduler::taskDone(Task *task)
{
	/* Sample execution time of the task, from launch to notifyDone */
	std::chrono::milliseconds milliseconds =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - task->launchTime_);

	LOG(Task, Debug) << "Task " << task->id() << " executed in "
			 << milliseconds.count() << "ms";

	taskDone_.emit(task);

	runningTasks_.erase(task);
	removeFromGroupTasks(task);

	for (auto *succedent : task->succedents_) {
		if (0 == succedent->removeDependency(task)) {
			runningTasks_.emplace(succedent);
			pendingTasks_.erase(succedent);

			succedent->launch();
		}
	}

	tasksHolder_.erase(task);
}

/**
 * \class CategorizedScheduler
 * \brief A Scheduler that uses an enum Category as the Task's group ID
 */

/**
 * \fn CategorizedScheduler::CategorizedScheduler(
 *      const std::map<Category, std::string> &categoryNames)
 * \brief Construct a CategorizedScheduler with category names
 * \param[in] categoryNames The map from enum group IDs to group names
 */

/**
 * \fn void CategorizedScheduler::queueTask(Task *task, Category group)
 * \brief Call Scheduler::queueTask with Category enum as the group ID
 * \param[in] task The task being queued and will be executed
 * \param[in] group The group ID in enum Category that the Task belongs to
 */

/**
 * \fn std::list<Task *> &CategorizedScheduler::groupTasks(Category group)
 * \brief Call Scheduler::groupTasks with Category enum as the group ID
 * \param[in] group The group ID in enum Category
 */

/**
 * \fn void CategorizedScheduler::succeedPrevTaskByStep(Category group, size_t step, Task *task)
 * \brief Call Scheduler::succeedPrevTaskByStep with Category enum as the group ID
 * \param[in] group The group ID in enum Category
 * \param[in] step The number of Tasks to trace back, 0 being the latest one
 * \param[in] task The Task that needs to add a dependency
 */

} /* namespace libcamera */
