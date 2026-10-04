#include "BusMasterKernel.h"
#include "../BusmasterDBNetwork\BusMasterNetWork.h"
#define defBusmaster_Dil_Dll_Name       "BusmasterDriverInterface.dll"
#define defBusmaster_Dil_GetDil_Func    "GetDilInterface"
#include "..\BusmasterDriverInterface\Include\BaseDIL_CAN.h"
#include "..\BusmasterDriverInterface\Include\BaseDIL_LIN.h"
#include "..\BusmasterDriverInterface\Include\BaseDIL_FLEXRAY.h"
#include "..\BusmasterDriverInterface\Include\BaseDIL_J1939.h"

BusMasterKernel* BusMasterKernel::mKernel = nullptr;

BusMasterKernel* BusMasterKernel::create()
{
    if ( nullptr == mKernel )
    {
        mKernel = new BusMasterKernel();
    }
    return mKernel;
}


BusMasterKernel::BusMasterKernel()
{
    mDIL_GetInterface = nullptr;
    mBmNetworkService = new BMNetwork();
    loadDilInterface();
}


BusMasterKernel::~BusMasterKernel()
{
}



HRESULT BusMasterKernel::getBusService( ETYPE_BUS busType, IBusService** busService )
{
    if (busService == nullptr)
    {
        return S_FALSE;
    }
    *busService = nullptr;
    if (nullptr != mDIL_GetInterface)
    {
        return mDIL_GetInterface(busType, busService);
    }
    return S_FALSE;
}
HRESULT BusMasterKernel::getDatabaseService( IBMNetWorkService** dbService )
{
    *dbService = mBmNetworkService;
    return S_FALSE;
}
bool BusMasterKernel::loadDilInterface()
{
    bool result = true;

    if ( nullptr == mDIL_GetInterface )
    {
        mDIL_GetInterface = nullptr;
        mDriverLibrary = LoadLibrary( defBusmaster_Dil_Dll_Name );
        if ( nullptr == mDriverLibrary )
        {
            result = false;
        }
        mDIL_GetInterface = (pDIL_GetInterface)GetProcAddress( mDriverLibrary, defBusmaster_Dil_GetDil_Func );
        if ( nullptr == mDIL_GetInterface )
        {
            result = false;
        }
    }
    return result;

}


KERNEL_USAGEMODE HRESULT __cdecl getBusmasterKernel( IBusMasterKernel** kernel )
{
    *kernel = BusMasterKernel::create();
    return S_OK;
}

KERNEL_USAGEMODE HRESULT __cdecl DIL_GetInterface( ETYPE_BUS eBusType, void** ppvInterface )
{
    if (ppvInterface == nullptr)
    {
        return S_FALSE;
    }
    *ppvInterface = nullptr;

    IBusMasterKernel* kernel = nullptr;
    getBusmasterKernel( &kernel );
    if ( nullptr == kernel )
    {
        return S_FALSE;
    }

    IBusService* busService = nullptr;
    if (FAILED(kernel->getBusService( eBusType, &busService )) || busService == nullptr)
    {
        return S_FALSE;
    }

    *ppvInterface = busService;
    return S_OK;
}

