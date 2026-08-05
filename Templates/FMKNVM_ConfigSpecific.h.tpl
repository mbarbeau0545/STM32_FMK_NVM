/*********************************************************************
 * @file        FMKNVM_ConfigSpecific.h
 * @brief       Board-specific generated integration for FMK_NVM.
 * @note        Copy or generate this template into FMKCFG_ConfigSpecific.
 *********************************************************************/
#ifndef FMKNVM_CONFIGSPECIFIC_H_INCLUDED
#define FMKNVM_CONFIGSPECIFIC_H_INCLUDED

#include "FMK_HAL/FMK_NVM/Src/FMKNVM_Backend.h"
#include "FMK_CFG/FMKCFG_ConfigFiles/FMKNVM_ConfigPublic.h"

/// @brief Defines the physical backend range of one logical partition.
typedef struct __t_sFMKNVM_PartitionStorageCfg
{
    t_uint32 storageOffset_u32;  ///< Backend-relative partition offset.
    t_uint32 storageSize_u32;    ///< Physical bytes reserved by the partition.
    t_uint16 slotCount_u16;      ///< Number of transactional rotating slots.
} t_sFMKNVM_PartitionStorageCfg;

/**
 * @brief Return the backend selected for the generated board configuration.
 * @note The returned API and context remain valid for the complete FMK_NVM
 *       lifetime.The callback table is immutable from the module viewpoint.
 *
 * @retval Valid pointer Selected immutable backend API.
 * @retval NULL No backend is available for FMKNVM_EEPROM_TYPE.
 */
const t_sFMKNVM_BackendApi * FMKNVM_Specific_GetBackendApi(void);

/**
 * @brief Copy the physical storage mapping of one logical partition.
 * @note The mapping belongs to the backend selected by FMKNVM_EEPROM_TYPE.
 *
 * @param[in]  f_PartitionId_e : Logical partition requiring storage.
 * @param[out] f_StorageCfg_ps : Caller-owned destination receiving the map.
 *
 * @retval RC_OK                  @ref RC_OK
 * @retval RC_ERROR_PTR_NULL      @ref RC_ERROR_PTR_NULL
 * @retval RC_ERROR_PARAM_INVALID @ref RC_ERROR_PARAM_INVALID
 * @retval RC_ERROR_NOT_SUPPORTED @ref RC_ERROR_NOT_SUPPORTED
 */
t_eReturnCode FMKNVM_Specific_GetPartitionStorage(   t_eFMKNVM_PartitionId f_PartitionId_e,
                                                     t_sFMKNVM_PartitionStorageCfg * f_StorageCfg_ps);

#endif // FMKNVM_CONFIGSPECIFIC_H_INCLUDED
