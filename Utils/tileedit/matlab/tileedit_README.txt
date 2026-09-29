tileedit.m is a visual inspection utility for planetary texture tiles.
It contains some rudimentary capabilities for editing elevation data.

Build the ddsread MEX function first (mex mex/ddsread/ddsread.cpp), then run tileedit in MATLAB.
A standalone program: mcc -m tileedit.m elvread.m elvmodread.m elvmodwrite.m ddsread.mexa64
(it needs the MATLAB Runtime of the same release).
