// not upstream: collision addon, the factory of the real CollSdk (CollSdkOrbiter.cpp); Orbiter-free so CollPlugin and CollSession include it freely
#ifndef __COLLSDKORBITER_H
#define __COLLSDKORBITER_H
#include <memory>
#include "CollSdk.h"
std::unique_ptr<CollSdk> CollSdkOrbiterCreate (bool processMode); // session instance (false) or the process instance of E4 (true)
void CollSdkOrbiterBindClient (CollSdk &sdk);                     // clbkSimulationStart, session instance: gcCore binding (1.6)
#endif
