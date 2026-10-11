#pragma once

#include "CANDriverDefines.h"

HRESULT UdsServer_Show(HWND hParent);
void UdsServer_OnCan(const STCAN_MSG& msg);
