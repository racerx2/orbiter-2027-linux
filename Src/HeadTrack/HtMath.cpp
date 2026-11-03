// not upstream (head-track): ported from linuxtrack's math_utils.c (MIT, LICENSE-linuxtrack)

#include "HtMath.h"
#include <cmath>
#include <utility>

namespace ht {

void MakeVec (const double pt1[3], const double pt2[3], double res[3])
{
	res[0] = pt1[0] - pt2[0];
	res[1] = pt1[1] - pt2[1];
	res[2] = pt1[2] - pt2[2];
}

double VecSize (const double vec[3])
{
	return std::sqrt (vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2]);
}

double Dot (const double vec1[3], const double vec2[3])
{
	return vec1[0]*vec2[0] + vec1[1]*vec2[1] + vec1[2]*vec2[2];
}

void Cross (const double vec1[3], const double vec2[3], double res[3])
{
	res[0] = vec1[1]*vec2[2] - vec1[2]*vec2[1];
	res[1] = vec1[2]*vec2[0] - vec1[0]*vec2[2];
	res[2] = vec1[0]*vec2[1] - vec1[1]*vec2[0];
}

void MulVec (const double vec[3], double c, double res[3])
{
	res[0] = vec[0]*c;
	res[1] = vec[1]*c;
	res[2] = vec[2]*c;
}

void NormalizeVec (double vec[3])
{
	double a = VecSize (vec);
	vec[0] /= a;
	vec[1] /= a;
	vec[2] /= a;
}

void AssignMatrix (const double src[3][3], double tgt[3][3])
{
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			tgt[i][j] = src[i][j];
}

void MakeBase (const double vec1[3], const double vec2[3], double res[3][3])
{
	double tmp1[3], tmp2[3];
	MulVec (vec1, 1/VecSize (vec1), res[0]);
	MulVec (res[0], Dot (vec2, res[0]), tmp1);
	MakeVec (vec2, tmp1, tmp2);
	MulVec (tmp2, 1/VecSize (tmp2), res[1]);
	Cross (res[0], res[1], res[2]);
}

void MatrixTimesVec (const double m[3][3], const double vec[3], double res[3])
{
	res[0] = m[0][0] * vec[0] + m[0][1] * vec[1] + m[0][2] * vec[2];
	res[1] = m[1][0] * vec[0] + m[1][1] * vec[1] + m[1][2] * vec[2];
	res[2] = m[2][0] * vec[0] + m[2][1] * vec[1] + m[2][2] * vec[2];
}

double Sqr (double f)
{
	return f*f;
}

void MulMatrix (const double m1[3][3], const double m2[3][3], double res[3][3])
{
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			res[i][j] = m1[i][0]*m2[0][j] + m1[i][1]*m2[1][j] + m1[i][2]*m2[2][j];
}

void Transpose (const double matrix[3][3], double trans[3][3])
{
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			trans[i][j] = matrix[j][i];
}

void TransposeInPlace (double matrix[3][3])
{
	std::swap (matrix[0][1], matrix[1][0]);
	std::swap (matrix[0][2], matrix[2][0]);
	std::swap (matrix[1][2], matrix[2][1]);
}

void MatrixToEuler (const double matrix[3][3], double *pitch, double *yaw, double *roll)
{
	double tmp = matrix[0][2];
	if (tmp < -1.0) tmp = -1.0;
	if (tmp > 1.0) tmp = 1.0;
	*yaw = std::asin (tmp);
	double yc = std::cos (*yaw);
	if (std::fabs (yc) > 1e-5) {
		*pitch = std::atan2 (-matrix[1][2]/yc, matrix[2][2]/yc);
		*roll = std::atan2 (-matrix[0][1]/yc, matrix[0][0]/yc);
	} else {
		*pitch = 0.0;
		*roll = std::atan2 (matrix[1][0], matrix[1][1]);
	}
}

void EulerToMatrix (double pitch, double yaw, double roll, double matrix[3][3])
{
	double cp = std::cos (pitch), sp = std::sin (pitch);
	double cy = std::cos (yaw), sy = std::sin (yaw);
	double cr = std::cos (roll), sr = std::sin (roll);
	matrix[0][0] = cr * cy;
	matrix[0][1] = -sr * cy;
	matrix[0][2] = sy;
	matrix[1][0] = cr * sy * sp + sr * cp;
	matrix[1][1] = -sr * sy * sp + cr * cp;
	matrix[1][2] = -cy * sp;
	matrix[2][0] = -cr * sy * cp + sr * sp;
	matrix[2][1] = sr * sy * cp + cr * sp;
	matrix[2][2] = cy * cp;
}

void AddVecs (const double vec1[3], const double vec2[3], double res[3])
{
	res[0] = vec1[0] + vec2[0];
	res[1] = vec1[1] + vec2[1];
	res[2] = vec1[2] + vec2[2];
}

bool IsMatrixFinite (const double matrix[3][3])
{
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			if (!IsFinite (matrix[i][j])) return false;
	return true;
}

void InvertMatrix (const double in[3][3], double out[3][3])
{
	double det = in[0][0] * in[1][1] * in[2][2] - in[2][0] * in[1][1] * in[0][2] +
	             in[1][0] * in[2][1] * in[0][2] - in[1][0] * in[0][1] * in[2][2] +
	             in[2][0] * in[0][1] * in[1][2] - in[0][0] * in[2][1] * in[1][2];
	Cross (in[1], in[2], out[0]);
	Cross (in[2], in[0], out[1]);
	Cross (in[0], in[1], out[2]);
	MulVec (out[0], 1/det, out[0]);
	MulVec (out[1], 1/det, out[1]);
	MulVec (out[2], 1/det, out[2]);
	TransposeInPlace (out);
}

bool IsVectorFinite (const double vec[3])
{
	return IsFinite (vec[0]) && IsFinite (vec[1]) && IsFinite (vec[2]);
}

bool IsFinite (double f)
{
	return std::isfinite (f);
}

double ClampAngle (double angle)
{
	if (angle < -180.0) return -180.0;
	if (angle > 180.0) return 180.0;
	return angle;
}

float NonLinFilt (float x, float y_minus_1, float filterfactor)
{
	if (!IsFinite (x)) return y_minus_1;
	float delta = x - y_minus_1;
	float y = y_minus_1 + delta * (std::fabs (delta)/(std::fabs (delta) + filterfactor));
	if (!IsFinite (y)) return IsFinite (y_minus_1) ? y_minus_1 : 0.0f;
	return y;
}

} // namespace ht
