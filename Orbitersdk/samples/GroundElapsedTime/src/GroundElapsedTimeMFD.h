/***************************************************************************
  GET Delay MFD page: clickable controls for the GET offset (see
  GroundElapsedTimeOffset.h for the sign convention).
  ***************************************************************************/

#ifndef __GROUNDELAPSEDTIMEMFD_H
#define __GROUNDELAPSEDTIMEMFD_H

// Registers / unregisters the "GET Delay" MFD mode (opcDLLInit / opcDLLExit).
void GroundElapsedTimeMFDInit(HINSTANCE hDLL);
void GroundElapsedTimeMFDExit();

#endif // !__GROUNDELAPSEDTIMEMFD_H
