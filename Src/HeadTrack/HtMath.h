// not upstream (head-track): vectors, 3x3 matrices and filters for the pose, ported from linuxtrack's math_utils.c (MIT, LICENSE-linuxtrack)

#ifndef __HTMATH_H
#define __HTMATH_H

namespace ht {

void MakeVec (const double pt1[3], const double pt2[3], double res[3]); // pt1 - pt2
double VecSize (const double vec[3]);
double Dot (const double vec1[3], const double vec2[3]);
void Cross (const double vec1[3], const double vec2[3], double res[3]);
void MulVec (const double vec[3], double c, double res[3]);
void NormalizeVec (double vec[3]);
void AssignMatrix (const double src[3][3], double tgt[3][3]);
void MakeBase (const double vec1[3], const double vec2[3], double res[3][3]); // orthonormal: vec1, vec2's part normal to it, their cross product
void MatrixTimesVec (const double m[3][3], const double vec[3], double res[3]);
double Sqr (double f);
void MulMatrix (const double m1[3][3], const double m2[3][3], double res[3][3]);
void Transpose (const double matrix[3][3], double trans[3][3]);
void TransposeInPlace (double matrix[3][3]);
void MatrixToEuler (const double matrix[3][3], double *pitch, double *yaw, double *roll);
void EulerToMatrix (double pitch, double yaw, double roll, double matrix[3][3]);
void AddVecs (const double vec1[3], const double vec2[3], double res[3]);
bool IsMatrixFinite (const double matrix[3][3]);
void InvertMatrix (const double in[3][3], double out[3][3]);
bool IsVectorFinite (const double vec[3]);
bool IsFinite (double f);
double ClampAngle (double angle);             // degrees, to -180..180
float NonLinFilt (float x, float y_minus_1, float filterfactor); // a step that small moves slowly, a big one at once

} // namespace ht

#endif // !__HTMATH_H
