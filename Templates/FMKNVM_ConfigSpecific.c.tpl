/*********************************************************************
 * @file        FMKNVM_ConfigSpecific.c
 * @brief       Board-specific generated integration for FMK_NVM.
 *********************************************************************/
#include "FMK_CFG/FMKCFG_ConfigSpecific/FmkNvmConfigSpecific/FMKNVM_ConfigSpecific.h"
#include "FMK_CFG/FMKCFG_ConfigFiles/FMKNVM_ConfigPrivate.h"

/* CAUTION : Automatic generated code section for Backend Include: Start */

/* CAUTION : Automatic generated code section for Backend Include: End */

/* CAUTION : Automatic generated code section for Storage Tables: Start */

/* CAUTION : Automatic generated code section for Storage Tables: End */

/*********************************
 * FMKNVM_Specific_GetBackendApi
 *********************************/
const t_sFMKNVM_BackendApi * FMKNVM_Specific_GetBackendApi(void)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = NULL;

    //---- 1- Select the configured backend API ----//
    switch(FMKNVM_EEPROM_TYPE)
    {
        /* CAUTION : Automatic generated code section for Backend API: Start */

        /* CAUTION : Automatic generated code section for Backend API: End */

        case FMKNVM_EEPROM_TYPE_NB:
        default:
        {
            //---- 2- Keep a null API for an unsupported selection ----//
        }
        break;
    }

    return BackendApi_ps;
}

/*********************************
 * FMKNVM_Specific_GetPartitionStorage
 *********************************/
t_eReturnCode FMKNVM_Specific_GetPartitionStorage(   t_eFMKNVM_PartitionId f_PartitionId_e,
                                                     t_sFMKNVM_PartitionStorageCfg * f_StorageCfg_ps)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the caller-owned destination ----//
    if(f_StorageCfg_ps == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_PartitionId_e >= FMKNVM_PARTITION_NB)
    {
        Ret_e = RC_ERROR_PARAM_INVALID;
    }
    else
    {
        //---- 2- Select the physical mapping for the configured backend ----//
        switch(FMKNVM_EEPROM_TYPE)
        {
            /* CAUTION : Automatic generated code section for Storage Map: Start */

            /* CAUTION : Automatic generated code section for Storage Map: End */

            case FMKNVM_EEPROM_TYPE_NB:
            default:
            {
                Ret_e = RC_ERROR_NOT_SUPPORTED;
            }
            break;
        }
    }

    return Ret_e;
}
