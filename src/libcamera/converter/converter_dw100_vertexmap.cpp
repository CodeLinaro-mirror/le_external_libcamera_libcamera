#include "libcamera/internal/converter/converter_dw100_vertexmap.h"

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <stdint.h>
#include <tuple>
#include <vector>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

#include <libcamera/geometry.h>

#include "libcamera/internal/vector.h"

constexpr int kDw100BlockSize = 16;

namespace libcamera {

LOG_DECLARE_CATEGORY(Converter)

using Vector2d = Vector<double, 2>;

namespace {
void applyTransform(const Transform &t, const double w, const double h,
		    Vector2d &p)
{
	if (!!(t & Transform::HFlip)) {
		p.x() = w - p.x();
	}

	if (!!(t & Transform::VFlip)) {
		p.y() = h - p.y();
	}

	if (!!(t & Transform::Transpose)) {
		std::swap(p.x(), p.y());
	}
}

void applyTransform(const Transform &t, Rectangle &s)
{
	if (!!(t & Transform::Transpose)) {
		std::swap(s.width, s.height);
	}
}

int roundTowardsZero(double v)
{
	double r = (v < 0) ? std::ceil(v) : std::floor(v);
	return static_cast<int>(r);
}

Vector2d rotatedRectSize(const Vector2d &size, const double rad)
{
	double sa = sin(rad);
	double ca = cos(rad);

	return { { std::abs(size.x() * ca) + std::abs(size.y() * sa),
		   std::abs(size.x() * sa) + std::abs(size.y() * ca) } };
}

Vector2d point2Vec2d(const Point &p)
{
	return { { static_cast<double>(p.x), static_cast<double>(p.y) } };
}

void transformPoint(const Rectangle &from, const Rectangle &to, Vector2d &p)
{
	double sx = to.width / static_cast<double>(from.width);
	double sy = to.height / static_cast<double>(from.height);
	p.x() = (p.x() - from.x) * sx + to.x;
	p.y() = (p.y() - from.y) * sy + to.y;
}

} /* namespace */

/**
 * The vertex map class represents a helper for handling dewarper vertex maps.
 * There are 3 important sizes in the system:
 *
 * - The Sensor size. The number of pixels of the whole sensor (Todo specify if
 *   OB lines are included)
 * - The input Rectangle to the dewarper. Describes the pixel data
 *   flowing into the dewarper in sensor space
 * - ScalerCrop rectangle. The rectangle that shall be used for all further
 *   stages. It is applied after lens dewarping but is in sensor coordinate
 *   space.
 * - The output size. This defines the size, the dewarper should output.
 *
 * +------------------------+
 * |Sensor size             |
 * |                        |
 * |   +----------------+   |
 * |   |  Input rect    |   |
 * |   |                |   |
 * |   +----------------+   |
 * +------------------------+
 *
 * This class implements a vertex map that forms the following pipeline:
 *
 * +-------------+    +-------------+    +-----------+    +--------------------+
 * | Lens Dewarp | -> | Scaler Crop | -> | Transform | -> | Offset, Scale, Rot |
 * +-------------+    +-------------+    +-----------+    +--------------------+
 *
 * ToDo: LensDewarp is not yet implemented. An identity map is used instead.
 *
 * All parameters are clamped to valid values before creating the vertex map.
 *
 * The constrains process works as follows:
 * - The ScalerCrop rectangle is clamped to the input rectangle
 * - The ScalerCrop rectangle is transformed by the specified transform
 *   forming ScalerCropT
 * - A rectangle of output size is placed in the center of ScalerCropT
 *   (OutputRect).
 * - Rotate gets applied to OutputRect,
 * - Scale is applied, but clamped so that the OutputRect fits completely into
 *   ScalerCropT (Only regarding dimensions, not position)
 * - Offset is clamped so that the OutputRect lies inside ScalerCropT
 *
 * The lens dewarp map is usually calibrated during tuning and is a map that
 * maps from incoming pixels to dewarped pixels.
 *
 * Dewarp modes:
 * - Crop: Fills the output frame with the data from ScalerCropT
 *      - Preserves aspect ratio
 *      - Rotate and Offset and Scale are taken into account within the limits.
 * - Fill:
 *      - Fills the output frame with the data from ScalerCropT.
 *      - Does not preserve aspect ratio
 *      - Rotate is taken into account
 *
 */
void Dw100VertexMap::applyLimits()
{
	int ow = outputSize_.width;
	int oh = outputSize_.height;
	effectiveScalerCrop_ = scalerCrop_.boundedTo(sensorCrop_);

	/* Map the scalerCrop to the input pixel space */
	Rectangle localScalerCropT = effectiveScalerCrop_.transformedBetween(
		sensorCrop_, Rectangle(inputSize_));

	applyTransform(transform_, localScalerCropT);

	double rad = rotation_ / 180.0 * M_PI;
	double sa = sin(rad);
	double ca = cos(rad);

	double scale = scale_;
	Vector2d offset;
	Vector2d size = rotatedRectSize(point2Vec2d(Point(ow, oh)), rad);
	Vector2d t;

	/*
         * Todo: All these rotations and calculations below are way easier using
         * matrices. So reimplement using matrix class.
         */

	/*
         * Calculate constraints
         */
	if (mode_ == Crop) {
		/* Scale up if needed */
		scale = std::max(scale,
				 std::max(size.x() / localScalerCropT.width,
					  size.y() / localScalerCropT.height));
		effectiveScaleX_ = scale;
		effectiveScaleY_ = scale;

		size = size / scale;

		/* Transform offset to sensor space */
		offset.x() = ca * offset_.x - sa * offset_.y;
		offset.y() = sa * offset_.x + ca * offset_.y;
		offset = offset / scale;
	} else if (mode_ == Fill) {
		/*
		 * Calculate the best x and y scale values to fit the rotated
		 * localScalerCropT rectangle into the output rectangle.
		 */
		double diff = (static_cast<double>(localScalerCropT.width) -
			       localScalerCropT.height) *
			      0.5;
		double middle = (static_cast<double>(localScalerCropT.width) +
				 localScalerCropT.height) *
				0.5;
		double w = middle + cos(rad * 2) * diff;
		double h = middle - cos(rad * 2) * diff;

		size = rotatedRectSize(Vector2d{ { w, h } }, rad);
		scale = std::max(size.x() / localScalerCropT.width,
				 size.y() / localScalerCropT.height);

		effectiveScaleX_ = (ow / w) * scale;
		effectiveScaleY_ = (oh / h) * scale;

		size = size / scale;

		t = point2Vec2d(offset_);
		t.x() /= effectiveScaleX_;
		t.y() /= effectiveScaleY_;

		/* Transform offset to sensor space including local scale */
		offset.x() = ca * t.x() - sa * t.y();
		offset.y() = sa * t.x() + ca * t.y();
	} else {
		LOG(Converter, Error) << "Unknown mode " << mode_;
		return;
	}

	/* Clamp offset in input space. */
	double maxoff;
	/*
	 * Due to rounding errors, size might be slightly bigger than scaler
	 * crop. Clamp the offset to 0 to prevent a crash in the next clamp.
	 */
	maxoff = std::max(0.0, (localScalerCropT.width - size.x())) * 0.5;
	offset.x() = std::clamp(offset.x(), -maxoff, maxoff);
	maxoff = std::max(0.0, (localScalerCropT.height - size.y())) * 0.5;
	offset.y() = std::clamp(offset.y(), -maxoff, maxoff);

	/*
	 * Transform offset back into output space.
	 * Note the transposed rotation matrix.
	 */
	t = offset;
	offset.x() = ca * t.x() + sa * t.y();
	offset.y() = -sa * t.x() + ca * t.y();
	offset.x() *= effectiveScaleX_;
	offset.y() *= effectiveScaleY_;

	effectiveOffset_ = Point(roundTowardsZero(offset.x()),
				 roundTowardsZero(offset.y()));
}

std::vector<uint32_t> Dw100VertexMap::getVertexMap()
{
	int ow = outputSize_.width;
	int oh = outputSize_.height;
	int tileCountW = getVerticesForLength(ow);
	int tileCountH = getVerticesForLength(oh);
	double rad = rotation_ / 180.0 * M_PI;
	double sa = sin(rad);
	double ca = cos(rad);

	applyLimits();

	/*
	 * libcamera handles all crop rectangles in sensor space. But the
	 * dewarper "sees" only the pixels it gets passed. Note that these might
	 * not cover exactly the max sensor crop, as there might be a crop
	 * between ISP and dewarper to crop to a format supported by the
	 * dewarper. effectiveScalerCrop_ is the crop in sensor space that gets
	 * fed into the dewarper. localScalerCrop is the sensor crop mapped to
	 * the data that is fed into the dewarper.
	 */
	Rectangle localScalerCrop = effectiveScalerCrop_.transformedBetween(
		sensorCrop_, Rectangle(inputSize_));
	Rectangle localScalerCropT = localScalerCrop;
	applyTransform(transform_, localScalerCropT);

	/*
	 * The dw100 has a specialty in interpolation that has to be taken into
	 * account to use in a pixel perfect manner. To explain this, I will
	 * only use the x direction, the vertical axis behaves the same.
	 *
	 * Let's start with a pixel perfect 1:1 mapping of an image with a width
	 * of 64pixels. The coordinates of the vertex map would then be:
	 * 0 -- 16 -- 32 -- 48 -- 64
	 * Note how the last coordinate lies outside the image (which ends at
	 * 63) as it is basically the beginning of the next macro block.
	 *
	 * if we zoom out a bit we might end up with something like
	 * -10 -- 0 -- 32 -- 64 -- 74
	 * As the dewarper coordinates are unsigned it actually sees
	 * 0 -- 0 -- 32 -- 64 -- 74
	 * Leading to stretched pixels at the beginning and black for everything
	 * > 63
	 *
	 * Now lets rotate the image by 180 degrees. A trivial rotation would
	 * end up with:
	 *
	 * 64 -- 48 -- 32 -- 16 -- 0
	 *
	 * But as the first column now points to pixel 64 we get a single black
	 * line. So for a proper 180* rotation, the coordinates need to be
	 *
	 * 63 -- 47 -- 31 -- 15 -- -1
	 *
	 * The -1 is clamped to 0 again, leading to a theoretical slight
	 * interpolation error on the last 16 pixels.
	 *
	 * To create this proper transformation there are two things todo:
	 *
	 * 1. The rotation centers are offset by -0.5. This evens out for no rotation,
	 * and leads to a coordinate offset of -1 on 180 degree rotations.
	 *
	 * 2. The transformation (flip and transpose) need to act on a width-1
	 * to get the same effect.
	 */
	Vector2d centerS{ { localScalerCropT.width * 0.5 - 0.5,
			    localScalerCropT.height * 0.5 - 0.5 } };
	Vector2d centerD{ { ow * 0.5 - 0.5,
			    oh * 0.5 - 0.5 } };

	LOG(Converter, Debug)
		<< "Apply vertex map for"
		<< " inputSize: " << inputSize_
		<< " outputSize: " << outputSize_
		<< " Transform: " << transformToString(transform_)
		<< "\n effectiveScalerCrop: " << effectiveScalerCrop_
		<< " localScalerCropT: " << localScalerCropT
		<< " scaleX: " << effectiveScaleX_
		<< " scaleY: " << effectiveScaleX_
		<< " rotation: " << rotation_
		<< " offset: " << effectiveOffset_;

	/*
	 * For every output tile, calculate the position of the corners in the
	 * input image.
	 */
	std::vector<uint32_t> res;
	res.reserve(tileCountW * tileCountH);
	for (int y = 0; y < tileCountH; y++) {
		for (int x = 0; x < tileCountW; x++) {
			Vector2d p{ { static_cast<double>(x) * kDw100BlockSize,
				      static_cast<double>(y) * kDw100BlockSize } };
			p = p.max(0.0).min(Vector2d{ { static_cast<double>(ow),
						       static_cast<double>(oh) } });

			/*
			 * Transform into coordinates centered on the output
			 * rectangle + offset.
			 */
			p = p - centerD + point2Vec2d(effectiveOffset_);
			p.x() /= effectiveScaleX_;
			p.y() /= effectiveScaleY_;

			/* Rotate */
			Vector2d p2;
			p2.x() = ca * p.x() - sa * p.y();
			p2.y() = sa * p.x() + ca * p.y();

			/* Transform to localScalerCropT space. */
			p = p2 + centerS;
			/* Subtract -1 for the mapping documented above. */
			applyTransform(-transform_,
				       localScalerCropT.width - 1,
				       localScalerCropT.height - 1, p);
			p.x() += localScalerCrop.x;
			p.y() += localScalerCrop.y;

			if (dewarpParamsValid_ && lensDewarpEnable_) {
				/*
				 * We are in localScalerCrop coordinates.
				 * Tranform to sensor coordinates.
				 */
				transformPoint(localScalerCrop, effectiveScalerCrop_, p);
				p = dewarpPoint(p);
				/* tarnsform back to localScalerCrop */
				transformPoint(effectiveScalerCrop_, localScalerCrop, p);
			}

			/* Convert to fixed point */
			uint32_t v = static_cast<uint32_t>(p.y() * 16) << 16 |
				     (static_cast<uint32_t>(p.x() * 16) & 0xffff);
			res.push_back(v);
		}
	}

	return res;
}

int Dw100VertexMap::loadDewarpParams(const YamlObject &dict)
{
	Matrix<double, 3, 3> m;
	const auto &cm = dict["cm"].get<Matrix<double, 3, 3>>();
	if (!cm) {
		LOG(Converter, Error) << "Dewarp parameters are missing 'cm' value";
		return -EINVAL;
	}

	m = *cm;

	const auto &coeffs = dict["coefficients"].getList<double>();
	if (!coeffs) {
		LOG(Converter, Error) << "Dewarp parameters 'coefficients' value is not a list";
		return -EINVAL;
	}

	const Span<const double> span{ *coeffs };
	return setDewarpParams(m, span);
}

int Dw100VertexMap::setDewarpParams(const Matrix<double, 3, 3> &cm, const Span<const double> &coeffs)
{
	dewarpM_ = cm;
	dewarpCoeffs_.fill(0.0);

	if (coeffs.size() != 4 && coeffs.size() != 5 &&
	    coeffs.size() != 8 && coeffs.size() != 12) {
		LOG(Converter, Error) << "Dewarp 'coefficients' must have 4, 5, 8 or 12 values";
		dewarpParamsValid_ = false;
		return -EINVAL;
	}
	std::copy(coeffs.begin(), coeffs.end(), dewarpCoeffs_.begin());

	dewarpParamsValid_ = true;
	return 0;
}

Vector2d Dw100VertexMap::dewarpPoint(const Vector2d &p)
{
	double x, y;
	double k1 = dewarpCoeffs_[0];
	double k2 = dewarpCoeffs_[1];
	double p1 = dewarpCoeffs_[2];
	double p2 = dewarpCoeffs_[3];
	double k3 = dewarpCoeffs_[4];
	double k4 = dewarpCoeffs_[5];
	double k5 = dewarpCoeffs_[6];
	double k6 = dewarpCoeffs_[7];
	double s1 = dewarpCoeffs_[8];
	double s2 = dewarpCoeffs_[9];
	double s3 = dewarpCoeffs_[10];
	double s4 = dewarpCoeffs_[11];

	y = (p.y() - dewarpM_[1][2]) / dewarpM_[1][1];
	x = (p.x() - dewarpM_[0][2] - y * dewarpM_[0][1]) / dewarpM_[0][0];

	double r2 = x * x + y * y;
	double d = (1 + k1 * r2 + k2 * r2 * r2 + k3 * r2 * r2 * r2) /
		   (1 + k4 * r2 + k5 * r2 * r2 + k6 * r2 * r2 * r2);
	x = x * d + 2 * p1 * x * y + p2 * (r2 + 2 * x * x) + s1 * r2 + s2 * r2 * r2;
	y = y * d + 2 * p2 * x * y + p1 * (r2 + 2 * y * y) + s3 * r2 + s4 * r2 * r2;

	Vector2d ret{ { x * dewarpM_[0][0] + y * dewarpM_[0][1] + dewarpM_[0][2],
			y * dewarpM_[1][1] + dewarpM_[1][2] } };
	return ret;
}

int Dw100VertexMap::getVerticesForLength(const int length)
{
	return (length + kDw100BlockSize - 1) / kDw100BlockSize + 1;
}

} /* namespace libcamera */
