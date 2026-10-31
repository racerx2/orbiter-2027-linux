// not upstream (nose-wheel): a vessel's wheeled nose touchdown point at taxi speed and under nosewheel steering (Vessel::AddSurfaceForces)
#ifndef __NOSEWHEEL_H
#define __NOSEWHEEL_H

namespace NoseWheel {

const double HOLD = 0.5;  // up to this point speed the nose wheel holds as the other points [m/s]
const double FREE = 5.0;  // without steering it swivels up to this speed, full sideways grip from twice it [m/s]
const double RATE = 1.0;  // steering command change a second (upstream's MAX_NOSE_MOVEMENT)

bool IsWheel (double mu_lng, double mu);              // rolls more easily than it slides
double SteerMax (double v);                           // steering angle at full command at point speed v [rad]: 60 deg to 5 m/s, 300 deg/v, at least 6 deg
double Grip (double v, double steer);                 // share of the sideways grip: 1 while steering, else 0 to FREE rising to 1 at 2 FREE
double Follow (double cur, double cmd, double dt);    // the steering command toward cmd at RATE
void Frame (double delta, double vlon, double vlat, double &vw, double &vn);       // ground speed along the wheel and across it
void ToVessel (double delta, double fw, double fn, double &flon, double &flat);     // forces along and across the wheel to the vessel's

}

#endif // !__NOSEWHEEL_H
