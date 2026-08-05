/*********************************************************************
 * @file        FMK_NVM.h
 * @brief       Logical-object persistent storage module.
 *********************************************************************/
#ifndef FMKNVM_INCLUDED
#define FMKNVM_INCLUDED

#include "FMK_CFG/FMKCFG_ConfigFiles/FMKNVM_ConfigPublic.h"
#include "TypeCommon.h"

/// @brief Exposes the fixed mapping of one logical object.
typedef struct __t_sFMKNVM_ObjectInfo
{
    t_eFMKNVM_PartitionId partitionId_e; ///< Owning partition.
    t_uint32 dataSize_u32;                ///< Exact logical object size.
    t_uint16 version_u16;                 ///< Logical object format version.
    t_bool isAvailable_b;                 ///< Object cache is available.
} t_sFMKNVM_ObjectInfo;

/// @brief Reports runtime diagnostics for one persistent partition.
typedef struct __t_sFMKNVM_Diagnostics
{
    t_uint32 successfulCommitCount_u32; ///< Successful commit count.
    t_uint32 failedCommitCount_u32;     ///< Failed commit count.
    t_uint32 unchangedWriteCount_u32;   ///< Ignored unchanged object writes.
    t_uint32 activeSequence_u32;        ///< Latest valid record sequence.
    t_uint16 activeSlot_u16;            ///< Slot containing the valid record.
    t_eReturnCode lastResult_e;          ///< Last detailed NVM result.
    t_bool isDirty_b;                    ///< Cache differs from storage.
    t_bool isOperationPending_b;         ///< A commit is pending.
    t_bool hasValidRecord_b;             ///< A persisted record is available.
} t_sFMKNVM_Diagnostics;

/**
 * @brief Arm the FMK_NVM configuration state machine.
 * @note No backend access is performed by this function.
 * @retval RC_OK Runtime data were reset successfully.
 * @retval RC_ERROR_COPY_FAILED Safe memory initialisation failed.
 * @retval RC_ERROR_MISSING_CONFIG Physical partition mapping is unavailable.
 */
t_eReturnCode FMKNVM_Init(void);

/**
 * @brief Execute one bounded step of the FMK_NVM state machines.
 * @retval RC_OK Current state processing completed.
 * @retval RC_WARNING_PENDING Processing requires another cyclic call.
 * @retval RC_ERROR_MISSING_CONFIG No backend is configured.
 */
t_eReturnCode FMKNVM_Cyclic(void);

/**
 * @brief Read the public module state.
 * @param[out] f_State_pe : Destination for the current cyclic state.
 * @retval RC_OK State was returned.
 * @retval RC_ERROR_PTR_NULL Output pointer is null.
 */
t_eReturnCode FMKNVM_GetState(t_eCyclicModState * f_State_pe);

/**
 * @brief Request a public module state.
 * @param[in] f_State_e : New cyclic module state.
 * @retval RC_OK State was stored.
 * @retval RC_ERROR_PARAM_INVALID State value is invalid.
 */
t_eReturnCode FMKNVM_SetState(t_eCyclicModState f_State_e);

/**
 * @brief Read the fixed configuration and availability of one logical object.
 * @param[in] f_ObjectId_e : Configured logical object identifier.
 * @param[out] f_ObjectInfo_ps : Destination object information.
 * @retval RC_OK Information was returned.
 * @retval RC_ERROR_PTR_NULL Output pointer is null.
 * @retval RC_ERROR_PARAM_INVALID Object identifier is invalid.
 */
t_eReturnCode FMKNVM_GetObjectInfo(   t_eFMKNVM_ObjectId f_ObjectId_e,
                                      t_sFMKNVM_ObjectInfo *
                                      f_ObjectInfo_ps);

/**
 * @brief Copy one logical object from its partition cache.
 * @note FMK_NVM does not retain the caller-owned destination buffer. The
 *       supplied size must equal the configured fixed object size.
 * @param[in] f_ObjectId_e : Configured logical object identifier.
 * @param[out] f_Data_pv : Caller-owned destination buffer.
 * @param[in] f_DataSize_u32 : Exact destination and object size in bytes.
 * @retval RC_OK The object was copied.
 * @retval RC_WARNING_NO_OPERATION The partition cache is unavailable.
 * @retval RC_ERROR_PTR_NULL Destination pointer is null.
 * @retval RC_ERROR_PARAM_INVALID Identifier or size is invalid.
 */
t_eReturnCode FMKNVM_GetObject(   t_eFMKNVM_ObjectId f_ObjectId_e,
                                  void * f_Data_pv,
                                  t_uint32 f_DataSize_u32);

/**
 * @brief Copy one logical object into its partition cache.
 * @note The supplied size must equal the configured fixed object size.
 *       FMK_NVM compares the cached bytes, marks the owning partition dirty
 *       only after a real change, and applies the partition commit policy from
 *       FMKNVM_Cyclic. The caller-owned source pointer is never retained.
 * @param[in] f_ObjectId_e : Configured logical object identifier.
 * @param[in] f_Data_pv : Caller-owned source buffer.
 * @param[in] f_DataSize_u32 : Exact source and object size in bytes.
 * @retval RC_OK The object changed and the partition was marked dirty.
 * @retval RC_WARNING_NO_OPERATION The cached object was already identical.
 * @retval RC_ERROR_PTR_NULL Source pointer is null.
 * @retval RC_ERROR_PARAM_INVALID Identifier or size is invalid.
 */
t_eReturnCode FMKNVM_SetObject(   t_eFMKNVM_ObjectId f_ObjectId_e,
                                  const void * f_Data_pv,
                                  t_uint32 f_DataSize_u32);

/**
 * @brief Read the dirty state of one transactional partition.
 * @param[in] f_PartitionId_e : Configured partition identifier.
 * @param[out] f_IsDirty_pb : Destination dirty flag.
 * @retval RC_OK Dirty state was returned.
 * @retval RC_ERROR_PTR_NULL Output pointer is null.
 * @retval RC_ERROR_PARAM_INVALID Partition identifier is invalid.
 */
t_eReturnCode FMKNVM_IsPartitionDirty(   t_eFMKNVM_PartitionId f_PartitionId_e,
                                         t_bool * f_IsDirty_pb);

/**
 * @brief Read current diagnostics of one transactional partition.
 * @param[in] f_PartitionId_e : Configured partition identifier.
 * @param[out] f_Diagnostics_ps : Destination diagnostic snapshot.
 * @retval RC_OK Diagnostics were returned.
 * @retval RC_ERROR_PTR_NULL Output pointer is null.
 * @retval RC_ERROR_PARAM_INVALID Partition identifier is invalid.
 */
t_eReturnCode FMKNVM_GetPartitionDiagnostics(   t_eFMKNVM_PartitionId f_PartitionId_e,
                                                t_sFMKNVM_Diagnostics * f_Diagnostics_ps);

#endif // FMKNVM_INCLUDED
