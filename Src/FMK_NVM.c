/*********************************************************************
 * @file        FMK_NVM.c
 * @brief       Opaque byte-image persistent storage module.e.
 *              
 *
 * @author      mba
 * @date        07/08/2026
 * @version     1.0
 */
  
// ********************************************************************
// *                      Includes
// ********************************************************************
#include "Library/SafeMem/SafeMem.h"
#include "Library/FIFO/Src/LIBFifo.h"
#include "Library/CRC/CRC.h"
#include "FMK_NVM.h"
#include "FMKNVM_Backend.h"
#include "FMK_HAL/FMK_CPU/Src/FMK_CPU.h"
#include "FMK_HAL/FMK_SRL/Src/FMK_SRL.h"
#include "FMK_CFG/FMKCFG_ConfigFiles/FMKNVM_ConfigPrivate.h"
#include "FMK_CFG/FMKCFG_ConfigSpecific/FmkNvmConfigSpecific/FMKNVM_ConfigSpecific.h"
#include "APP_CTRL/APP_SYS/Src/APP_SYS.h"
// ********************************************************************
// *                      Defines
// ********************************************************************
/// @brief Serialized record header size in bytes.
#define FMKNVM_RECORD_HEADER_SIZE ((t_uint32)24U)

/// @brief Header bytes protected by the header CRC.
#define FMKNVM_RECORD_HEADER_CRC_DATA_SIZE ((t_uint16)20U)

/// @brief Serialized commit marker size in bytes.
#define FMKNVM_RECORD_MARKER_SIZE ((t_uint32)4U)

/// @brief CRC32 polynomial used by persistent records.
#define FMKNVM_CRC_POLY CRC32B_POLYNOME_ETH_ZIP_PNG

/// @brief CRC32 initial value used by persistent records.
#define FMKNVM_CRC_START CRC32B_STARTVALUE_ETH_ZIP_PNG

/// @brief Internal configuration and restoration sub-states.
typedef enum __t_eFMKNVM_InitState
{
    FMKNVM_CFG_BACKEND = 0U,
    FMKNVM_CFG_GEOMETRY,
    FMKNVM_CFG_CAPABILITIES,
    FMKNVM_CFG_LAYOUT,
    FMKNVM_CFG_RESTORE,
    FMKNVM_CFG_DONE,
    FMKNVM_CFG_ERROR
} t_eFMKNVM_InitState;
/// @brief Runtime backend-relative storage layout derived from backend geometry.
typedef struct __t_sFMKNVM_PartitionStorageLayout
{
    t_uint32 storageOffset_u32; ///< Backend-relative start offset of the partition.
    t_uint32 storageSize_u32;   ///< Total physical bytes reserved for the partition.
    t_uint32 slotStride_u32;    ///< Physical distance between two consecutive slots.
} t_sFMKNVM_PartitionStorageLayout;

/// @brief Runtime information for one transactional partition.
typedef struct __t_sFMKNVM_PartitionRuntime
{
    t_uint32 dataSize_u32;                          ///< Cached image size.
    t_uint32 currentGeneration_u32;                 ///< Current cache generation.
    t_uint32 committedGeneration_u32;               ///< Persisted generation.
    t_uint32 firstDirtyTick_u32;                     ///< First dirty timestamp.
    t_uint32 lastChangeTick_u32;                     ///< Latest change timestamp.
    t_uint32 lastCommitTick_u32;                     ///< Latest commit timestamp.
    t_bool dirty_b;                                  ///< Cache differs from NVM.
    t_bool commitPending_b;                          ///< Commit request is pending.
    t_bool hasValidRecord_b;                         ///< Valid record available.
    const t_sFMKNVM_PartitionDescriptor * desc_ps;  ///< Static logical configuration.
    t_sFMKNVM_PartitionStorageLayout storage_s;     ///< Derived physical storage layout.
} t_sFMKNVM_PartitionRuntime;

/// @brief Describes one queued automatic commit request.
typedef struct __t_sFMKNVM_CommitRequest
{
    t_eFMKNVM_PartitionId partitionId_e; ///< Object to persist.
} t_sFMKNVM_CommitRequest;

/// @brief Internal states of one transactional record commit.
typedef enum __t_eFMKNVM_CommitState
{
    FMKNVM_COMMIT_IDLE = 0U,
    FMKNVM_COMMIT_PREPARE,
    FMKNVM_COMMIT_ERASE,
    FMKNVM_COMMIT_INVALIDATE_MARKER,
    FMKNVM_COMMIT_PROGRAM_RECORD,
    FMKNVM_COMMIT_READ_RECORD,
    FMKNVM_COMMIT_CHECK_RECORD,
    FMKNVM_COMMIT_PROGRAM_MARKER,
    FMKNVM_COMMIT_READ_MARKER,
    FMKNVM_COMMIT_CHECK_MARKER,
    FMKNVM_COMMIT_FINALIZE,
    FMKNVM_COMMIT_ERROR,
    FMKNVM_COMMIT_STATE_NB
} t_eFMKNVM_CommitState;

// ********************************************************************
// *                      Variables
// ********************************************************************
/// @brief Current external module state.
static t_eCyclicModState g_FMKNVM_Sts_e = STATE_CYCLIC_CFG;
/// @brief Current configuration sub-state.
static t_eFMKNVM_InitState g_FMKNVM_CfgSts_e = FMKNVM_CFG_BACKEND;
/// @brief Selected backend contract.
static const t_sFMKNVM_BackendApi * g_FMKNVM_BackendApi_ps;
/// @brief Validated backend geometry.
static t_sFMKNVM_BackendGeometry g_FMKNVM_Geometry_s;
/// @brief Validated backend capabilities.
static t_sFMKNVM_BackendCapabilities g_FMKNVM_Capabilities_s;
/// @brief Runtime object states.
static t_sFMKNVM_PartitionRuntime g_FMKNVM_Runtime_as[FMKNVM_PARTITION_NB];
/// @brief Per-object diagnostics.
static t_sFMKNVM_Diagnostics g_FMKNVM_Diagnostics_as[FMKNVM_PARTITION_NB];
/// @brief FIFO management for pending commit requests.
static t_sLIBFIFO_Core g_FMKNVM_CommitFifo_s;
/// @brief Storage owned by the commit request FIFO.
static t_sFMKNVM_CommitRequest g_FMKNVM_CommitRequests_as[FMKNVM_REQUEST_FIFO_SIZE];
/// @brief Next object examined by the automatic commit scheduler.
static t_uint16 g_FMKNVM_AutomaticPartitionIdx_u16;
/// @brief Object currently restored by the configuration state machine.
static t_uint16 g_FMKNVM_RestorePartitionIdx_u16;
/// @brief Current transactional commit state.
static t_eFMKNVM_CommitState g_FMKNVM_CommitState_e = FMKNVM_COMMIT_IDLE;
/// @brief Request currently removed from the FIFO and being committed.
static t_sFMKNVM_CommitRequest g_FMKNVM_ActiveRequest_s;
/// @brief Stable serialized header and payload of the active commit.
static t_uint8 g_FMKNVM_RecordBuffer_au8[FMKNVM_RECORD_HEADER_SIZE + FMKNVM_MAX_PARTITION_PAYLOAD_CAPACITY];
/// @brief Readback buffer used to verify the active record.
static t_uint8 g_FMKNVM_VerifyBuffer_au8[FMKNVM_RECORD_HEADER_SIZE + FMKNVM_MAX_PARTITION_PAYLOAD_CAPACITY];
/// @brief Serialized final marker or invalid marker.
static t_uint8 g_FMKNVM_MarkerBuffer_au8[FMKNVM_RECORD_MARKER_SIZE];
/// @brief Physical address of the active candidate slot.
static t_uint32 g_FMKNVM_ActiveSlotAddress_u32;
/// @brief Number of serialized header and payload bytes to program.
static t_uint32 g_FMKNVM_ActiveRecordSize_u32;
/// @brief Generation captured by the active commit snapshot.
static t_uint32 g_FMKNVM_SnapshotGeneration_u32;
/// @brief Physical slot selected for the active commit.
static t_uint16 g_FMKNVM_ActiveSlot_u16;
/// @brief A backend operation has returned RC_WARNING_PENDING.
static t_bool g_FMKNVM_BackendOperationPending_b;

//********************************************************************************
//                      Private functions - Declaration
//********************************************************************************
/**
 * @brief Restore the latest valid record of one persistent object.
 * @note Called exclusively by the configuration state machine.The cache owned
 *       by ConfigPrivate is populated; no application buffer is retained.
 * @param[in] f_PartitionId_e : Configured object to restore.
 * @retval RC_OK A valid record was restored, or no record is present.
 * @retval RC_ERROR_NVM_BACKEND_READ Backend storage could not be read.
 * @retval RC_ERROR_NVM_CRC_ERROR A candidate record failed integrity validation.
 */
static t_eReturnCode s_FMKNVM_RestorePartition(t_eFMKNVM_PartitionId f_PartitionId_e);
/**
 * @brief Execute exactly one configuration sub-state.
 * @note Backend Init, geometry, capabilities and restoration are advanced one
 *       sub-state per cyclic call.State transitions remain in FMKNVM_Cyclic.
 * @retval RC_OK Current sub-state completed.
 * @retval RC_WARNING_PENDING Backend requires another cycle.
 * @retval RC_ERROR_MISSING_CONFIG No backend was selected.
 */
static t_eReturnCode s_FMKNVM_ConfigurationState(void);
/**
 * @brief Execute one operational module cycle.
 * @note Services only the selected backend; public state transitions remain in
 *       FMKNVM_Cyclic.
 * @retval RC_OK Operational servicing completed.
 * @retval RC_WARNING_PENDING Backend work remains pending.
 */
static t_eReturnCode s_FMKNVM_OperationalState(void);
/**
 * @brief Schedule automatic commits according to each object time policy.
 * @note Called only by the operational cyclic state.The function examines
 *       one object per call with a round-robin index so execution time stays
 *       bounded.A dirty object is queued when the minimum commit interval is
 *       satisfied and either its quiet period or its maximum deferral has
 *       elapsed.A zero quiet period means immediate eligibility, a zero
 *       maximum deferral disables that deadline, and a zero minimum interval
 *       disables rate limiting.Unsigned tick subtraction supports tick wrap.
 *       The function never accesses the backend and never clears dirty data.
 *       A full FIFO leaves the object dirty for a later automatic retry.
 * @retval RC_OK No request was required.
 * @retval RC_WARNING_PENDING A commit request was queued.
 * @retval RC_WARNING_BUSY The FIFO is full; the object remains dirty.
 */
static t_eReturnCode s_FMKNVM_AutomaticCommitMngmt(void);
/**
 * @brief Evaluate whether one dirty object has reached its commit deadline.
 * @note Elapsed times use unsigned subtraction and therefore support tick
 *       counter wrap-around.
 * @param[in] f_Runtime_ps : Runtime and timing policy to evaluate.
 * @param[out] f_IsCommitDue_pb : TRUE when the object may be queued.
 * @retval RC_OK The scheduling decision was produced.
 * @retval RC_ERROR_PTR_NULL One input or output pointer is null.
 */
static t_eReturnCode s_FMKNVM_IsCommitDue(const t_sFMKNVM_PartitionRuntime * f_Runtime_ps, t_bool * f_IsCommitDue_pb);
/**
 * @brief Queue one object generation for persistence.
 * @param[in] f_PartitionId_e : Configured object identifier.
 * @retval RC_WARNING_PENDING Request was queued.
 * @retval RC_WARNING_NO_OPERATION Object is already clean or pending.
 * @retval RC_WARNING_BUSY Commit FIFO is full.
 */
static t_eReturnCode s_FMKNVM_EnqueueCommit(t_eFMKNVM_PartitionId f_PartitionId_e);
/**
 * @brief Progress one bounded step of the active commit transaction.
 * @note A stable snapshot is serialized before the first backend access.
 *       The dirty flag is cleared only when the committed generation still
 *       equals the current cache generation.
 * @retval RC_OK No operation is active or one step completed.
 * @retval RC_WARNING_PENDING A backend operation or commit remains active.
 * @retval RC_ERROR_NVM_BACKEND_PROGRAM Backend programming failed.
 * @retval RC_ERROR_NVM_BACKEND_ERASE Backend erasing failed.
 */
static t_eReturnCode s_FMKNVM_ProcessCommit(void);
/**
 * @brief Poll the completion of the active asynchronous backend operation.
 * @retval RC_OK The operation completed successfully.
 * @retval RC_WARNING_PENDING The backend remains busy.
 * @retval RC_ERROR_NVM_BACKEND_READ The active read failed.
 * @retval RC_ERROR_NVM_BACKEND_PROGRAM The active program failed.
 * @retval RC_ERROR_NVM_BACKEND_ERASE The active erase failed.
 */
static t_eReturnCode s_FMKNVM_CommitWaitBackendState(void);
/**
 * @brief Remove one automatic request from the FIFO.
 * @retval RC_OK One request became active.
 * @retval RC_WARNING_NO_OPERATION The FIFO is empty.
 */
static t_eReturnCode s_FMKNVM_CommitIdleState(void);
/**
 * @brief Create the stable serialized record for the active object.
 * @retval RC_OK The snapshot and record are ready.
 * @retval RC_ERROR_COPY_FAILED The cache snapshot could not be copied.
 */
static t_eReturnCode s_FMKNVM_CommitPrepareState(void);
/**
 * @brief Erase the slot selected for the active Flash transaction.
 * @retval RC_OK The slot was erased synchronously.
 * @retval RC_WARNING_PENDING The erase was accepted asynchronously.
 * @retval RC_ERROR_NVM_BACKEND_ERASE The erase failed.
 */
static t_eReturnCode s_FMKNVM_CommitEraseState(void);
/**
 * @brief Invalidate the marker of a reused non-erasable slot.
 * @retval RC_OK The marker was invalidated synchronously.
 * @retval RC_WARNING_PENDING Programming continues asynchronously.
 * @retval RC_ERROR_NVM_BACKEND_PROGRAM Programming failed.
 */
static t_eReturnCode s_FMKNVM_CommitInvalidateMarkerState(void);
/**
 * @brief Program the active serialized header and payload.
 * @retval RC_OK The record was programmed synchronously.
 * @retval RC_WARNING_PENDING Programming continues asynchronously.
 * @retval RC_ERROR_NVM_BACKEND_PROGRAM Programming failed.
 */
static t_eReturnCode s_FMKNVM_CommitProgramRecordState(void);
/**
 * @brief Read the active candidate record for verification.
 * @retval RC_OK The record was read synchronously.
 * @retval RC_WARNING_PENDING Reading continues asynchronously.
 * @retval RC_ERROR_NVM_BACKEND_READ Reading failed.
 */
static t_eReturnCode s_FMKNVM_CommitReadRecordState(void);
/**
 * @brief Compare the candidate record with its stable serialized source.
 * @retval RC_OK Every byte matches.
 * @retval RC_ERROR_NVM_NVM_VERIFY The readback differs.
 */
static t_eReturnCode s_FMKNVM_CommitCheckRecordState(void);
/**
 * @brief Program the final commit marker of the candidate slot.
 * @retval RC_OK The marker was programmed synchronously.
 * @retval RC_WARNING_PENDING Programming continues asynchronously.
 * @retval RC_ERROR_NVM_BACKEND_PROGRAM Programming failed.
 */
static t_eReturnCode s_FMKNVM_CommitProgramMarkerState(void);
/**
 * @brief Read the final commit marker for verification.
 * @retval RC_OK The marker was read synchronously.
 * @retval RC_WARNING_PENDING Reading continues asynchronously.
 * @retval RC_ERROR_NVM_BACKEND_READ Reading failed.
 */
static t_eReturnCode s_FMKNVM_CommitReadMarkerState(void);
/**
 * @brief Validate the final marker read from persistent storage.
 * @retval RC_OK The marker is valid.
 * @retval RC_ERROR_NVM_NVM_VERIFY The marker is invalid.
 */
static t_eReturnCode s_FMKNVM_CommitCheckMarkerState(void);
/**
 * @brief Publish and trace a successful commit in runtime and diagnostics.
 * @note The trace contains the partition identifier, return code and payload
 *       size written through the selected backend.
 * @retval RC_OK The committed generation was published.
 */
static t_eReturnCode s_FMKNVM_CommitFinalizeState(void);
/**
 * @brief Preserve dirty data and trace the failed commit diagnostics.
 * @note The trace contains the partition identifier, backend error and
 *       partition payload size retained for an automatic retry.
 * @retval RC_OK Failed transaction cleanup completed.
 */
static t_eReturnCode s_FMKNVM_CommitErrorState(void);
/**
 * @brief Store and assert one transaction failure in active diagnostics.
 * @param[in] f_Error_e : Negative return code produced by a commit state.
 */
static void
s_FMKNVM_RegisterCommitError(t_eReturnCode f_Error_e);
/**
 * @brief Store a 32-bit value in the persistent little-endian format.
 * @param[out] f_Data_pu8 : Destination containing at least four bytes.
 * @param[in] f_Value_u32 : Value to serialize.
 */
static void s_FMKNVM_WriteU32(t_uint8 * f_Data_pu8, t_uint32 f_Value_u32);
/**
 * @brief Store a 16-bit value in the persistent little-endian format.
 * @param[out] f_Data_pu8 : Destination containing at least two bytes.
 * @param[in] f_Value_u16 : Value to serialize.
 */
static void s_FMKNVM_WriteU16(t_uint8 * f_Data_pu8, t_uint16 f_Value_u16);
/**
 * @brief Read a 32-bit value from the persistent little-endian format.
 * @param[in] f_Data_pcu8 : Source containing at least four bytes.
 * @retval Deserialized 32-bit value.
 */
static t_uint32 s_FMKNVM_ReadU32(const t_uint8 * f_Data_pcu8);
/**
 * @brief Read a 16-bit value from the persistent little-endian format.
 * @param[in] f_Data_pcu8 : Source containing at least two bytes.
 * @retval Deserialized 16-bit value.
 */
static t_uint16 s_FMKNVM_ReadU16(const t_uint8 * f_Data_pcu8);
/**
 * @brief Compute the aligned commit marker offset inside one record slot.
 * @note The marker starts on its own backend commit unit so programming the
 *       record can never consume the marker's physical Flash word.
 * @param[in] f_Descriptor_ps : Object descriptor owning the record slot.
 * @retval Backend-unit-aligned marker offset from the slot start.
 */
static t_uint32 s_FMKNVM_GetMarkerOffset(   const t_sFMKNVM_PartitionDescriptor * f_Descriptor_ps);
/**
 * @brief Align one unsigned value upward without allowing 32-bit overflow.
 * @param[in] f_Value_u32 : Value to align.
 * @param[in] f_Alignment_u32 : Non-zero alignment.
 * @param[out] f_AlignedValue_pu32 : Aligned result.
 * @retval RC_OK Alignment completed.
 * @retval RC_ERROR_WRONG_CONFIG Alignment is zero or the aligned value overflows.
 */
static t_eReturnCode s_FMKNVM_AlignUp(t_uint32 f_Value_u32, t_uint32 f_Alignment_u32,
                                      t_uint32 * f_AlignedValue_pu32);
/**
 * @brief Derive every partition storage range from logical configuration and backend geometry.
 * @note No physical partition size or offset is stored in ConfigPrivate.
 * @retval RC_OK Every partition fits in the selected backend.
 * @retval RC_ERROR_WRONG_CONFIG Geometry/configuration is invalid or total storage is exceeded.
 */
static t_eReturnCode s_FMKNVM_ComputeStorageLayout(void);
/**
 * @brief Validate object partitions against the selected backend geometry.
 * @retval RC_OK Every configured partition and slot is usable.
 * @retval RC_ERROR_WRONG_CONFIG A partition is invalid or outside storage.
 */
static t_eReturnCode s_FMKNVM_ValidateConfiguration(void);

//********************************************************************************
//                      Public functions - Implementation
//********************************************************************************
/*********************************
 * FMKNVM_Init
 *********************************/
t_eReturnCode FMKNVM_Init(void)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 2- Reset every logical partition cache and runtime ----//
    for(t_uint16 idx_u16 = 0U ;
        (idx_u16 < (t_uint16)FMKNVM_PARTITION_NB) && (Ret_e == RC_OK) ;
        idx_u16++)
    {
        t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[idx_u16];

        Ret_e = SafeMem_memclear(Runtime_ps, (t_uint16)sizeof(*Runtime_ps));

        if(Ret_e == RC_OK)
        {
            t_sFMKNVM_Diagnostics * Diagnostics_ps = &g_FMKNVM_Diagnostics_as[idx_u16];

            Ret_e = SafeMem_memclear(Diagnostics_ps, (t_uint16)sizeof(*Diagnostics_ps));
        }

        if(Ret_e == RC_OK)
        {
            Runtime_ps->desc_ps = &c_FMKNVM_PartitionDescriptors_as[idx_u16];
            Ret_e = SafeMem_memclear(   Runtime_ps->desc_ps->cacheData_pu8,
                                        (t_uint16) Runtime_ps->desc_ps->payloadCapacity_u32);
        }
    }

    //---- 3- Create the commit request FIFO ----//
    if(Ret_e == RC_OK)
    {
        t_sLIBFIFO_Cfg FifoCfg_s;

        FifoCfg_s.bufferHead_pv = g_FMKNVM_CommitRequests_as;
        FifoCfg_s.elementSize_u16 = (t_uint16)sizeof(t_sFMKNVM_CommitRequest);
        FifoCfg_s.elementNb_u16 = FMKNVM_REQUEST_FIFO_SIZE;
        Ret_e = LIBFIFO_Create(&g_FMKNVM_CommitFifo_s, FifoCfg_s);
    }

    //---- 4- Arm the CFG state machine; no backend access is done here ----//
    if(Ret_e == RC_OK)
    {
        g_FMKNVM_AutomaticPartitionIdx_u16 = 0U;
        g_FMKNVM_RestorePartitionIdx_u16 = 0U;
        g_FMKNVM_CommitState_e = FMKNVM_COMMIT_IDLE;
        g_FMKNVM_BackendOperationPending_b = FALSE;
        g_FMKNVM_CfgSts_e = FMKNVM_CFG_BACKEND;
        g_FMKNVM_Sts_e = STATE_CYCLIC_CFG;
    }

    //---- 5- Report an initialization error to the assertion service ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

/*********************************
 * FMKNVM_Cyclic
 *********************************/
t_eReturnCode FMKNVM_Cyclic(void)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Dispatch the current public state ----//
    switch(g_FMKNVM_Sts_e)
    {
        case STATE_CYCLIC_CFG:
        {
            Ret_e = s_FMKNVM_ConfigurationState();

            //---- 2- Apply configuration state transition ----//
            if(g_FMKNVM_CfgSts_e == FMKNVM_CFG_DONE)
            {
                g_FMKNVM_Sts_e = STATE_CYCLIC_OPE;
            }
            else if((Ret_e < RC_OK) || (g_FMKNVM_CfgSts_e == FMKNVM_CFG_ERROR))
            {
                g_FMKNVM_Sts_e = STATE_CYCLIC_ERROR;
            }
        }
        break;
        case STATE_CYCLIC_OPE:
        {
            Ret_e = s_FMKNVM_OperationalState();
        }
        break;
        case STATE_CYCLIC_BUSY:
        {
        }
        break;
        case STATE_CYCLIC_PREOPE:
        case STATE_CYCLIC_ERROR:
        default:
        {
            //---- No action is allowed in this state ----//
        }
        break;
    }

    //---- 2- Report a cyclic processing error ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

/*********************************
 * FMKNVM_GetState
 *********************************/
t_eReturnCode FMKNVM_GetState(t_eCyclicModState * f_State_pe)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the output state pointer ----//
    if(f_State_pe == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else
    {
        //---- 2- Copy the current public module state ----//
        *f_State_pe = g_FMKNVM_Sts_e;
    }

    //---- 3- Report an invalid public API request ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

/*********************************
 * FMKNVM_SetState
 *********************************/
t_eReturnCode FMKNVM_SetState(t_eCyclicModState f_State_e)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Store the requested public module state ----//
    g_FMKNVM_Sts_e = f_State_e;

    return Ret_e;
}
/*********************************
 * FMKNVM_GetObjectInfo
 *********************************/
t_eReturnCode FMKNVM_GetObjectInfo(t_eFMKNVM_ObjectId f_ObjectId_e, t_sFMKNVM_ObjectInfo * f_Info_ps)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the output pointer ----//
    if(f_Info_ps == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_ObjectId_e >= FMKNVM_OBJECT_NB)
    {
        //---- 2- Validate the object identifier ----//
        Ret_e = RC_ERROR_PARAM_INVALID;
    }
    else if(g_FMKNVM_Sts_e != STATE_CYCLIC_OPE)
    {
        Ret_e = RC_WARNING_BUSY;
    }
    else
    {
        const t_sFMKNVM_ObjectDescriptor * ObjectDesc_ps = &c_FMKNVM_ObjectDescriptors_as[f_ObjectId_e];
        t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[ObjectDesc_ps->partitionId_e];
        t_uint32 validityByteIdx_u32 = (t_uint32)f_ObjectId_e / 8U;
        t_uint8 validityMask_u8 = (t_uint8)(1U << ((t_uint32)f_ObjectId_e % 8U));

        //---- 3- Copy the logical object mapping and availability ----//
        f_Info_ps->partitionId_e = ObjectDesc_ps->partitionId_e;
        f_Info_ps->dataSize_u32 = ObjectDesc_ps->size_u32;
        f_Info_ps->version_u16 = ObjectDesc_ps->version_u16;
        f_Info_ps->isAvailable_b = ((Runtime_ps->desc_ps->cacheData_pu8[validityByteIdx_u32] &
              validityMask_u8) != 0U);
    }

    //---- 4- Report an invalid object information request ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

/*********************************
 * FMKNVM_GetObject
 *********************************/
t_eReturnCode FMKNVM_GetObject(t_eFMKNVM_ObjectId f_ObjectId_e, void * f_Data_pv, t_uint32 f_DataSize_u32)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the caller-owned destination pointer ----//
    if(f_Data_pv == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_ObjectId_e >= FMKNVM_OBJECT_NB)
    {
        //---- 2- Validate the object identifier ----//
        Ret_e = RC_ERROR_PARAM_INVALID;
    }
    else if(g_FMKNVM_Sts_e != STATE_CYCLIC_OPE)
    {
        Ret_e = RC_WARNING_BUSY;
    }
    else
    {
        const t_sFMKNVM_ObjectDescriptor * ObjectDesc_ps = &c_FMKNVM_ObjectDescriptors_as[f_ObjectId_e];
        t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[ObjectDesc_ps->partitionId_e];
        t_uint32 validityByteIdx_u32 = (t_uint32)f_ObjectId_e / 8U;
        t_uint8 validityMask_u8 = (t_uint8)(1U << ((t_uint32)f_ObjectId_e % 8U));
        t_bool isObjectAvailable_b = ((Runtime_ps->desc_ps->cacheData_pu8[validityByteIdx_u32] &
              validityMask_u8) != 0U);

        //---- 3- Validate the exact object size and cache availability ----//
        if(f_DataSize_u32 != ObjectDesc_ps->size_u32)
        {
            Ret_e = RC_ERROR_PARAM_INVALID;
        }
        else if(isObjectAvailable_b == FALSE)
        {
            Ret_e = RC_WARNING_NVM_OBJECT_NOT_AVAILABLE;
        }
        else
        {
            const t_uint8 * CacheData_pcu8 = &Runtime_ps->desc_ps->cacheData_pu8[ObjectDesc_ps->offset_u32];

            //---- 4- Copy only the requested logical object ----//
            Ret_e = SafeMem_memcpy(f_Data_pv, CacheData_pcu8, (t_uint16)ObjectDesc_ps->size_u32);
        }
    }

    //---- 5- Report an object read error ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

/*********************************
 * FMKNVM_SetObject
 *********************************/
t_eReturnCode FMKNVM_SetObject(t_eFMKNVM_ObjectId f_ObjectId_e, const void * f_Data_pv, t_uint32 f_DataSize_u32)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the caller-owned source pointer ----//
    if(f_Data_pv == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_ObjectId_e >= FMKNVM_OBJECT_NB)
    {
        //---- 2- Validate the object identifier ----//
        Ret_e = RC_ERROR_PARAM_INVALID;
    }
    else if(g_FMKNVM_Sts_e != STATE_CYCLIC_OPE)
    {
        Ret_e = RC_WARNING_BUSY;
    }
    else
    {
        const t_sFMKNVM_ObjectDescriptor * ObjectDesc_ps = &c_FMKNVM_ObjectDescriptors_as[f_ObjectId_e];
        t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[ObjectDesc_ps->partitionId_e];
        t_uint32 validityByteIdx_u32 = (t_uint32)f_ObjectId_e / 8U;
        t_uint8 validityMask_u8 = (t_uint8)(1U << ((t_uint32)f_ObjectId_e % 8U));
        t_bool isObjectAvailable_b = ((Runtime_ps->desc_ps->cacheData_pu8[validityByteIdx_u32] &
              validityMask_u8) != 0U);

        //---- 3- Validate the exact configured object size ----//
        if(f_DataSize_u32 != ObjectDesc_ps->size_u32)
        {
            Ret_e = RC_ERROR_PARAM_INVALID;
        }
        else
        {
            t_uint8 * CacheData_pu8 = &Runtime_ps->desc_ps->cacheData_pu8[ObjectDesc_ps->offset_u32];
            t_bool isUnchanged_b = FALSE;

            //---- 4- Compare the object with its partition cache bytes ----//
            if(isObjectAvailable_b == TRUE)
            {
                Ret_e = SafeMem_memcmp(CacheData_pu8, f_Data_pv, (t_uint16)ObjectDesc_ps->size_u32);

                if(Ret_e == RC_OK)
                {
                    isUnchanged_b = TRUE;
                }
                else if(Ret_e == RC_WARNING_MEM_FAILED)
                {
                    Ret_e = RC_OK;
                }
            }

            if(isUnchanged_b == TRUE)
            {
                g_FMKNVM_Diagnostics_as[ObjectDesc_ps->partitionId_e]
                    .unchangedWriteCount_u32++;
                Ret_e = RC_WARNING_NO_OPERATION;
            }
            else if(Ret_e == RC_OK)
            {
                //---- 5- Copy the object into the owning partition cache ----//
                Ret_e = SafeMem_memcpy(CacheData_pu8, f_Data_pv, (t_uint16)ObjectDesc_ps->size_u32);

                if(Ret_e == RC_OK)
                {
                    t_uint32 currentTick_u32;

                    //---- 6- Timestamp the changed partition generation ----//
                    FMKCPU_GetTick(&currentTick_u32);

                    if(Runtime_ps->dirty_b == FALSE)
                    {
                        Runtime_ps->firstDirtyTick_u32 = currentTick_u32;
                    }

                    Runtime_ps->lastChangeTick_u32 = currentTick_u32;
                    Runtime_ps->desc_ps->cacheData_pu8[validityByteIdx_u32] |= validityMask_u8;
                    Runtime_ps->dataSize_u32 = Runtime_ps->desc_ps->payloadCapacity_u32;
                    Runtime_ps->currentGeneration_u32++;
                    Runtime_ps->dirty_b = TRUE;
                }
            }
        }
    }

    //---- 7- Report an object cache update error ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

/*********************************
 * FMKNVM_IsPartitionDirty
 *********************************/
t_eReturnCode FMKNVM_IsPartitionDirty(t_eFMKNVM_PartitionId f_PartitionId_e, t_bool * f_IsDirty_pb)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the output pointer ----//
    if(f_IsDirty_pb == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_PartitionId_e >= FMKNVM_PARTITION_NB)
    {
        //---- 2- Validate the partition identifier ----//
        Ret_e = RC_ERROR_PARAM_INVALID;
    }
    else if(g_FMKNVM_Sts_e != STATE_CYCLIC_OPE)
    {
        Ret_e = RC_WARNING_BUSY;
    }
    else
    {
        t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[f_PartitionId_e];

        //---- 3- Return the current dirty flag ----//
        *f_IsDirty_pb = Runtime_ps->dirty_b;
    }

    //---- 4- Report an invalid partition state request ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

/*********************************
 * FMKNVM_GetPartitionDiagnostics
 *********************************/
t_eReturnCode FMKNVM_GetPartitionDiagnostics(   t_eFMKNVM_PartitionId f_PartitionId_e,
                                                t_sFMKNVM_Diagnostics * f_Diagnostics_ps)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the output pointer ----//
    if(f_Diagnostics_ps == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_PartitionId_e >= FMKNVM_PARTITION_NB)
    {
        //---- 2- Validate the partition identifier ----//
        Ret_e = RC_ERROR_PARAM_INVALID;
    }
    else if(g_FMKNVM_Sts_e != STATE_CYCLIC_OPE)
    {
        Ret_e = RC_WARNING_BUSY;
    }
    else
    {
        t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[f_PartitionId_e];

        //---- 3- Copy the current diagnostic snapshot ----//
        g_FMKNVM_Diagnostics_as[f_PartitionId_e].isDirty_b = Runtime_ps->dirty_b;
        g_FMKNVM_Diagnostics_as[f_PartitionId_e].isOperationPending_b = Runtime_ps->commitPending_b;
        g_FMKNVM_Diagnostics_as[f_PartitionId_e].hasValidRecord_b = Runtime_ps->hasValidRecord_b;
        *f_Diagnostics_ps = g_FMKNVM_Diagnostics_as[f_PartitionId_e];
    }

    //---- 4- Report an invalid diagnostic request ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_RestorePartition
 *********************************/
static t_eReturnCode s_FMKNVM_RestorePartition( t_eFMKNVM_PartitionId f_PartitionId_e)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = g_FMKNVM_BackendApi_ps;
    void * BackendContext_pv = BackendApi_ps->context_pv;
    t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[f_PartitionId_e];
    const t_sFMKNVM_PartitionStorageLayout * Storage_ps = &Runtime_ps->storage_s;
    t_bool isRecordFound_b = FALSE;
    t_uint32 bestSequence_u32 = 0U;
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Scan every configured slot of the selected object ----//
    for(t_uint16 slot_u16 = 0U ;
        (slot_u16 < Runtime_ps->desc_ps->slotCount_u16) && (Ret_e == RC_OK) ;
        slot_u16++)
    {
        t_uint32 slotStride_u32 = Storage_ps->slotStride_u32;
        t_uint32 slotAddress_u32 = Storage_ps->storageOffset_u32 +
            ((t_uint32)slot_u16 * slotStride_u32);
        t_uint32 markerAddress_u32 = slotAddress_u32 +
            s_FMKNVM_GetMarkerOffset(Runtime_ps->desc_ps);

        Ret_e = BackendApi_ps->Read_pcb(   BackendContext_pv,
                                           slotAddress_u32,
                                           g_FMKNVM_RecordBuffer_au8,
                                           FMKNVM_RECORD_HEADER_SIZE);

        if(Ret_e == RC_OK)
        {
            Ret_e = BackendApi_ps->Read_pcb(   BackendContext_pv,
                                               markerAddress_u32,
                                               g_FMKNVM_MarkerBuffer_au8,
                                               FMKNVM_RECORD_MARKER_SIZE);
        }

        if(Ret_e == RC_OK)
        {
            t_uint32 magic_u32 = s_FMKNVM_ReadU32(&g_FMKNVM_RecordBuffer_au8[0U]);
            t_uint16 version_u16 = s_FMKNVM_ReadU16(&g_FMKNVM_RecordBuffer_au8[4U]);
            t_uint16 partitionId_u16 = s_FMKNVM_ReadU16(&g_FMKNVM_RecordBuffer_au8[6U]);
            t_uint32 sequence_u32 = s_FMKNVM_ReadU32(&g_FMKNVM_RecordBuffer_au8[8U]);
            t_uint32 payloadSize_u32 = s_FMKNVM_ReadU32(&g_FMKNVM_RecordBuffer_au8[12U]);
            t_uint32 storedCrc_u32 = s_FMKNVM_ReadU32(&g_FMKNVM_RecordBuffer_au8[16U]);
            t_uint32 storedHeaderCrc_u32 = s_FMKNVM_ReadU32(&g_FMKNVM_RecordBuffer_au8[20U]);
            t_uint32 marker_u32 = s_FMKNVM_ReadU32(g_FMKNVM_MarkerBuffer_au8);
            t_uint32 computedHeaderCrc_u32;
            t_bool isHeaderValid_b;

            //---- 2- Validate the serialized header integrity ----//
            Ret_e = LIBCRC_ComputeCrc32Bits(   g_FMKNVM_RecordBuffer_au8,
                                               FMKNVM_RECORD_HEADER_CRC_DATA_SIZE,
                                               CRC32B_POLYNOME_ETH_ZIP_PNG,
                                               CRC32B_STARTVALUE_ETH_ZIP_PNG,
                                               &computedHeaderCrc_u32);

            if(Ret_e == RC_OK)
            {
                isHeaderValid_b = (magic_u32 == FMKNVM_RECORD_MAGIC) && (version_u16 ==
                     Runtime_ps->desc_ps->formatVersion_u16) && (partitionId_u16 == (t_uint16)f_PartitionId_e) &&
                    (payloadSize_u32 == Runtime_ps->desc_ps->payloadCapacity_u32) &&
                    (storedHeaderCrc_u32 == computedHeaderCrc_u32) && (marker_u32 == FMKNVM_COMMIT_MARKER);
            }
            else
            {
                isHeaderValid_b = FALSE;
            }

            if(isHeaderValid_b == TRUE)
            {
                t_uint8 * Payload_pu8 = &g_FMKNVM_RecordBuffer_au8[FMKNVM_RECORD_HEADER_SIZE];
                t_uint32 computedCrc_u32;

                //---- 3- Read and validate the candidate payload ----//
                Ret_e = BackendApi_ps->Read_pcb(   BackendContext_pv,
                                                   slotAddress_u32 + FMKNVM_RECORD_HEADER_SIZE,
                                                   Payload_pu8,
                                                   payloadSize_u32);

                if(Ret_e == RC_OK)
                {
                    Ret_e = LIBCRC_ComputeCrc32Bits(   Payload_pu8,
                                                       (t_uint16)payloadSize_u32,
                                                       FMKNVM_CRC_POLY,
                                                       FMKNVM_CRC_START,
                                                       &computedCrc_u32);
                }

                if((Ret_e == RC_OK) && (computedCrc_u32 == storedCrc_u32))
                {
                    t_uint32 sequenceDelta_u32 = sequence_u32 - bestSequence_u32;
                    t_bool isNewer_b = (isRecordFound_b == FALSE) || ((sequenceDelta_u32 != 0U) &&
                         (sequenceDelta_u32 < 0x80000000UL));

                    if(isNewer_b == TRUE)
                    {
                        t_uint8 * CacheData_pu8 = Runtime_ps->desc_ps->cacheData_pu8;

                        //---- 4- Restore the newest valid candidate ----//
                        Ret_e = SafeMem_memcpy(CacheData_pu8, Payload_pu8, (t_uint16)payloadSize_u32);

                        if(Ret_e == RC_OK)
                        {
                            Runtime_ps->dataSize_u32 = payloadSize_u32;
                            Runtime_ps->currentGeneration_u32 = sequence_u32;
                            Runtime_ps->committedGeneration_u32 = sequence_u32;
                            Runtime_ps->hasValidRecord_b = TRUE;
                            bestSequence_u32 = sequence_u32;
                            isRecordFound_b = TRUE;
                            g_FMKNVM_Diagnostics_as[f_PartitionId_e]
                                .activeSlot_u16 = slot_u16;
                            g_FMKNVM_Diagnostics_as[f_PartitionId_e]
                                .activeSequence_u32 = sequence_u32;

                            FMKSRL_LOG("[NVM] : Retrieve partition %d from slot %d\r\n", f_PartitionId_e, slot_u16);
                        }
                    }
                }
            }
            else 
            {
                FMKSRL_LOG( "[NVM] : Header of slot %d from partition %d incorrect\r\n",
                            slot_u16,
                            f_PartitionId_e);
            }
        }
    }

    //---- 5- Report a physical restoration error ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

//********************************************************************************
//                      Private functions - Implementation
//********************************************************************************

/*********************************
 * s_FMKNVM_ConfigurationState
 *********************************/
static t_eReturnCode s_FMKNVM_ConfigurationState(void)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = g_FMKNVM_BackendApi_ps;
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Execute one configuration sub-state ----//
    switch(g_FMKNVM_CfgSts_e)
    {
        case FMKNVM_CFG_BACKEND:
        {
            g_FMKNVM_BackendApi_ps = FMKNVM_Specific_GetBackendApi();

            if(BackendApi_ps == NULL)
            {
                Ret_e = RC_ERROR_MISSING_CONFIG;
            }
            else if((BackendApi_ps->Init_pcb == NULL) || (BackendApi_ps->GetGeometry_pcb == NULL) ||
                    (BackendApi_ps->GetCapabilities_pcb == NULL) || (BackendApi_ps->Read_pcb == NULL) ||
                    (BackendApi_ps->Program_pcb == NULL) || (BackendApi_ps->Erase_pcb == NULL) ||
                    (BackendApi_ps->IsBusy_pcb == NULL) || (BackendApi_ps->GetOperationResult_pcb == NULL))
            {
                Ret_e = RC_ERROR_MISSING_CONFIG;
            }
            else
            {
                Ret_e = BackendApi_ps->Init_pcb( BackendApi_ps->context_pv);
                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_CfgSts_e = FMKNVM_CFG_GEOMETRY;
                }
            }
        }
        break;
        case FMKNVM_CFG_GEOMETRY:
        {
            Ret_e = BackendApi_ps->GetGeometry_pcb(BackendApi_ps->context_pv, &g_FMKNVM_Geometry_s);
            if(Ret_e == RC_OK)
            {
                g_FMKNVM_CfgSts_e = FMKNVM_CFG_CAPABILITIES;
            }
        }
        break;
        case FMKNVM_CFG_CAPABILITIES:
        {
            void * BackendContext_pv = BackendApi_ps->context_pv;
            t_sFMKNVM_BackendCapabilities * Capabilities_ps = &g_FMKNVM_Capabilities_s;

            Ret_e = BackendApi_ps->GetCapabilities_pcb(BackendContext_pv, Capabilities_ps);
            if(Ret_e == RC_OK)
            {
                g_FMKNVM_CfgSts_e = FMKNVM_CFG_LAYOUT;
            }
        }
        break;
        case FMKNVM_CFG_LAYOUT:
        {
            //---- Derive physical ranges only after geometry and capabilities are known ----//
            Ret_e = s_FMKNVM_ComputeStorageLayout();

            if(Ret_e == RC_OK)
            {
                Ret_e = s_FMKNVM_ValidateConfiguration();
            }

            if(Ret_e == RC_OK)
            {
                g_FMKNVM_CfgSts_e = FMKNVM_CFG_RESTORE;
            }
        }
        break;
        case FMKNVM_CFG_RESTORE:
        {
            t_eFMKNVM_PartitionId PartitionId_e = (t_eFMKNVM_PartitionId)g_FMKNVM_RestorePartitionIdx_u16;

            Ret_e = s_FMKNVM_RestorePartition(PartitionId_e);

            if(Ret_e == RC_OK)
            {
                g_FMKNVM_RestorePartitionIdx_u16++;

                if(g_FMKNVM_RestorePartitionIdx_u16 >= (t_uint16)FMKNVM_PARTITION_NB)
                {
                    g_FMKNVM_CfgSts_e = FMKNVM_CFG_DONE;
                }
            }
        }
        break;
        default:
        {
            //---- No configuration action is required ----//
        }
        break;
    }
    if(Ret_e < RC_OK)
    {
        g_FMKNVM_CfgSts_e = FMKNVM_CFG_ERROR;
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_AutomaticCommitMngmt
 *********************************/
static t_eReturnCode s_FMKNVM_AutomaticCommitMngmt(void)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Select one object to keep the cyclic execution bounded ----//
    if(g_FMKNVM_AutomaticPartitionIdx_u16 < (t_uint16)FMKNVM_PARTITION_NB)
    {
        t_eFMKNVM_PartitionId PartitionId_e = (t_eFMKNVM_PartitionId)g_FMKNVM_AutomaticPartitionIdx_u16;
        t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[PartitionId_e];
        t_bool isCommitDue_b = FALSE;

        //---- 2- Advance the round-robin object index ----//
        g_FMKNVM_AutomaticPartitionIdx_u16++;

        if(g_FMKNVM_AutomaticPartitionIdx_u16 >= (t_uint16)FMKNVM_PARTITION_NB)
        {
            g_FMKNVM_AutomaticPartitionIdx_u16 = 0U;
        }

        //---- 3- Evaluate only dirty objects without a pending commit ----//
        if((Runtime_ps->dirty_b == TRUE) && (Runtime_ps->commitPending_b == FALSE))
        {
            Ret_e = s_FMKNVM_IsCommitDue(Runtime_ps, &isCommitDue_b);

            //---- 4- Queue the object only when its policy is satisfied ----//
            if((Ret_e == RC_OK) && (isCommitDue_b == TRUE))
            {
                Ret_e = s_FMKNVM_EnqueueCommit(PartitionId_e);
            }
        }
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_IsCommitDue
 *********************************/
static t_eReturnCode s_FMKNVM_IsCommitDue(const t_sFMKNVM_PartitionRuntime * f_Runtime_ps, t_bool * f_IsCommitDue_pb)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the scheduling inputs ----//
    if(f_Runtime_ps == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_IsCommitDue_pb == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else
    {
        t_uint32 currentTick_u32;
        t_uint32 quietElapsed_u32;
        t_uint32 deferredElapsed_u32;
        t_uint32 commitElapsed_u32;
        t_bool isQuietReached_b;
        t_bool isDeferredReached_b;
        t_bool isMinimumIntervalReached_b;

        //---- 2- Compute wrap-safe elapsed durations ----//
        FMKCPU_GetTick(&currentTick_u32);
        quietElapsed_u32 = currentTick_u32 - f_Runtime_ps->lastChangeTick_u32;
        deferredElapsed_u32 = currentTick_u32 - f_Runtime_ps->firstDirtyTick_u32;
        commitElapsed_u32 = currentTick_u32 - f_Runtime_ps->lastCommitTick_u32;

        //---- 3- Evaluate the quiet and maximum deferral deadlines ----//
        isQuietReached_b = (f_Runtime_ps->desc_ps->quietPeriodMs_u32 == 0U) || (quietElapsed_u32 >=
             f_Runtime_ps->desc_ps->quietPeriodMs_u32);
        isDeferredReached_b = (f_Runtime_ps->desc_ps->maximumDeferredCommitMs_u32 != 0U) && (deferredElapsed_u32 >=
             f_Runtime_ps->desc_ps->maximumDeferredCommitMs_u32);

        //---- 4- Apply the minimum interval between persisted versions ----//
        isMinimumIntervalReached_b = (f_Runtime_ps->committedGeneration_u32 == 0U) ||
            (f_Runtime_ps->desc_ps->minimumCommitIntervalMs_u32 == 0U) || (commitElapsed_u32 >=
             f_Runtime_ps->desc_ps->minimumCommitIntervalMs_u32);

        //---- 5- Publish the scheduling decision ----//
        *f_IsCommitDue_pb = (isMinimumIntervalReached_b == TRUE) && ((isQuietReached_b == TRUE) ||
             (isDeferredReached_b == TRUE));
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_EnqueueCommit
 *********************************/
static t_eReturnCode s_FMKNVM_EnqueueCommit(t_eFMKNVM_PartitionId f_PartitionId_e)
{
    t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[f_PartitionId_e];
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Reject clean objects and duplicate pending requests ----//
    if((Runtime_ps->dirty_b == FALSE) || (Runtime_ps->commitPending_b == TRUE))
    {
        Ret_e = RC_WARNING_NO_OPERATION;
    }
    else
    {
        t_sFMKNVM_CommitRequest Request_s;

        //---- 2- Capture the object selected by the scheduler ----//
        Request_s.partitionId_e = f_PartitionId_e;

        //---- 3- Queue the request without losing dirty data on failure ----//
        Ret_e = LIBFIFO_Push(&g_FMKNVM_CommitFifo_s, &Request_s, (t_uint16)sizeof(Request_s));

        if(Ret_e == RC_OK)
        {
            Runtime_ps->commitPending_b = TRUE;
            Ret_e = RC_WARNING_PENDING;
        }
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_ProcessCommit
 *********************************/
static t_eReturnCode s_FMKNVM_ProcessCommit(void)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Execute one bounded transaction state ----//
    switch(g_FMKNVM_CommitState_e)
    {
            case FMKNVM_COMMIT_IDLE:
            {
                Ret_e = s_FMKNVM_CommitIdleState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_PREPARE;
                    Ret_e = RC_WARNING_PENDING;
                }
                else if(Ret_e == RC_WARNING_NO_OPERATION)
                {
                    Ret_e = RC_OK;
                }
                else if(Ret_e < RC_OK)
                {
                    ASSERT((t_sint32)Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_IDLE;
                }
            }
            break;

            case FMKNVM_COMMIT_PREPARE:
            {
                Ret_e = s_FMKNVM_CommitPrepareState();

                if(Ret_e == RC_OK)
                {
                    if(g_FMKNVM_Capabilities_s.eraseRequired_b == TRUE)
                    {
                        g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERASE;
                    }
                    else
                    {
                        g_FMKNVM_CommitState_e = FMKNVM_COMMIT_INVALIDATE_MARKER;
                    }

                    Ret_e = RC_WARNING_PENDING;
                }
                else if(Ret_e < RC_OK)
                {
                    s_FMKNVM_RegisterCommitError(Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERROR;
                }
            }
            break;

            case FMKNVM_COMMIT_ERASE:
            {
                Ret_e = s_FMKNVM_CommitEraseState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_PROGRAM_RECORD;
                    Ret_e = RC_WARNING_PENDING;
                }
                else if(Ret_e == RC_WARNING_PENDING)
                {
                    g_FMKNVM_BackendOperationPending_b = TRUE;
                }
                else if(Ret_e < RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    s_FMKNVM_RegisterCommitError(Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERROR;
                }
            }
            break;

            case FMKNVM_COMMIT_INVALIDATE_MARKER:
            {
                Ret_e = s_FMKNVM_CommitInvalidateMarkerState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_PROGRAM_RECORD;
                    Ret_e = RC_WARNING_PENDING;
                }
                else if(Ret_e == RC_WARNING_PENDING)
                {
                    g_FMKNVM_BackendOperationPending_b = TRUE;
                }
                else if(Ret_e < RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    s_FMKNVM_RegisterCommitError(Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERROR;
                }
            }
            break;

            case FMKNVM_COMMIT_PROGRAM_RECORD:
            {
                Ret_e = s_FMKNVM_CommitProgramRecordState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_READ_RECORD;
                    Ret_e = RC_WARNING_PENDING;
                }
                else if(Ret_e == RC_WARNING_PENDING)
                {
                    g_FMKNVM_BackendOperationPending_b = TRUE;
                }
                else if(Ret_e < RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    s_FMKNVM_RegisterCommitError(Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERROR;
                }
            }
            break;

            case FMKNVM_COMMIT_READ_RECORD:
            {
                Ret_e = s_FMKNVM_CommitReadRecordState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_CHECK_RECORD;
                    Ret_e = RC_WARNING_PENDING;
                }
                else if(Ret_e == RC_WARNING_PENDING)
                {
                    g_FMKNVM_BackendOperationPending_b = TRUE;
                }
                else if(Ret_e < RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    s_FMKNVM_RegisterCommitError(Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERROR;
                }
            }
            break;

            case FMKNVM_COMMIT_CHECK_RECORD:
            {
                Ret_e = s_FMKNVM_CommitCheckRecordState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_PROGRAM_MARKER;
                    Ret_e = RC_WARNING_PENDING;
                }
                else if(Ret_e < RC_OK)
                {
                    s_FMKNVM_RegisterCommitError(Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERROR;
                }
            }
            break;

            case FMKNVM_COMMIT_PROGRAM_MARKER:
            {
                Ret_e = s_FMKNVM_CommitProgramMarkerState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_READ_MARKER;
                    Ret_e = RC_WARNING_PENDING;
                }
                else if(Ret_e == RC_WARNING_PENDING)
                {
                    g_FMKNVM_BackendOperationPending_b = TRUE;
                }
                else if(Ret_e < RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    s_FMKNVM_RegisterCommitError(Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERROR;
                }
            }
            break;

            case FMKNVM_COMMIT_READ_MARKER:
            {
                Ret_e = s_FMKNVM_CommitReadMarkerState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_CHECK_MARKER;
                    Ret_e = RC_WARNING_PENDING;
                }
                else if(Ret_e == RC_WARNING_PENDING)
                {
                    g_FMKNVM_BackendOperationPending_b = TRUE;
                }
                else if(Ret_e < RC_OK)
                {
                    g_FMKNVM_BackendOperationPending_b = FALSE;
                    s_FMKNVM_RegisterCommitError(Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERROR;
                }
            }
            break;

            case FMKNVM_COMMIT_CHECK_MARKER:
            {
                Ret_e = s_FMKNVM_CommitCheckMarkerState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_FINALIZE;
                    Ret_e = RC_WARNING_PENDING;
                }
                else if(Ret_e < RC_OK)
                {
                    s_FMKNVM_RegisterCommitError(Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERROR;
                }
            }
            break;

            case FMKNVM_COMMIT_FINALIZE:
            {
                Ret_e = s_FMKNVM_CommitFinalizeState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_IDLE;
                }
                else if(Ret_e < RC_OK)
                {
                    s_FMKNVM_RegisterCommitError(Ret_e);
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_ERROR;
                }
            }
            break;

            case FMKNVM_COMMIT_ERROR:
            {
                Ret_e = s_FMKNVM_CommitErrorState();

                if(Ret_e == RC_OK)
                {
                    g_FMKNVM_CommitState_e = FMKNVM_COMMIT_IDLE;
                }
            }
            break;

            default:
            {
                Ret_e = RC_ERROR_WRONG_STATE;
                ASSERT((t_sint32)Ret_e);
                g_FMKNVM_CommitState_e = FMKNVM_COMMIT_IDLE;
            }
            break;
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitWaitBackendState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitWaitBackendState(void)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = g_FMKNVM_BackendApi_ps;
    void * BackendContext_pv = BackendApi_ps->context_pv;
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Query the active backend operation ----//
    t_bool isBackendBusy_b = BackendApi_ps->IsBusy_pcb(BackendContext_pv);

    if(isBackendBusy_b == TRUE)
    {
        Ret_e = RC_WARNING_PENDING;
    }
    else
    {
        t_eReturnCode OperationResult_e = RC_OK;

        //---- 2- Retrieve the final physical operation result ----//
        Ret_e = BackendApi_ps->GetOperationResult_pcb(BackendContext_pv, &OperationResult_e);

        if(Ret_e == RC_OK)
        {
            Ret_e = OperationResult_e;
        }
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitIdleState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitIdleState(void)
{
    t_eReturnCode Ret_e;

    //---- 1- Activate the oldest automatic commit request ----//
    Ret_e = LIBFIFO_Pop(&g_FMKNVM_CommitFifo_s, &g_FMKNVM_ActiveRequest_s, (t_uint16)sizeof(g_FMKNVM_ActiveRequest_s));

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitPrepareState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitPrepareState(void)
{
    t_eFMKNVM_PartitionId PartitionId_e = g_FMKNVM_ActiveRequest_s.partitionId_e;
    t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[PartitionId_e];
    const t_sFMKNVM_PartitionStorageLayout * Storage_ps = &Runtime_ps->storage_s;
    t_uint8 * Header_pu8 = g_FMKNVM_RecordBuffer_au8;
    t_uint8 * Payload_pu8 = &g_FMKNVM_RecordBuffer_au8[FMKNVM_RECORD_HEADER_SIZE];
    t_uint32 payloadCrc_u32;
    t_uint32 headerCrc_u32;
    t_eReturnCode Ret_e;

    //---- 1- Copy a stable snapshot of the current cache ----//
    Ret_e = SafeMem_memcpy(Payload_pu8, Runtime_ps->desc_ps->cacheData_pu8, (t_uint16)Runtime_ps->dataSize_u32);

    if(Ret_e == RC_OK)
    {
        //---- 2- Compute the logical payload integrity code ----//
        Ret_e = LIBCRC_ComputeCrc32Bits(   Payload_pu8,
                                           (t_uint16)Runtime_ps->dataSize_u32,
                                           FMKNVM_CRC_POLY,
                                           FMKNVM_CRC_START,
                                           &payloadCrc_u32);
    }

    if(Ret_e == RC_OK)
    {
        //---- 3- Serialize the persistent record header ----//
        g_FMKNVM_SnapshotGeneration_u32 = Runtime_ps->currentGeneration_u32;
        s_FMKNVM_WriteU32(&Header_pu8[0U], FMKNVM_RECORD_MAGIC);
        s_FMKNVM_WriteU16(&Header_pu8[4U], Runtime_ps->desc_ps->formatVersion_u16);
        s_FMKNVM_WriteU16(&Header_pu8[6U], (t_uint16)PartitionId_e);
        s_FMKNVM_WriteU32(&Header_pu8[8U], g_FMKNVM_SnapshotGeneration_u32);
        s_FMKNVM_WriteU32(&Header_pu8[12U], Runtime_ps->dataSize_u32);
        s_FMKNVM_WriteU32(&Header_pu8[16U], payloadCrc_u32);

        //---- 4- Protect the serialized metadata with its own CRC ----//
        Ret_e = LIBCRC_ComputeCrc32Bits(   Header_pu8,
                                           FMKNVM_RECORD_HEADER_CRC_DATA_SIZE,
                                           FMKNVM_CRC_POLY,
                                           FMKNVM_CRC_START,
                                           &headerCrc_u32);
    }

    if(Ret_e == RC_OK)
    {
        t_uint32 slotStride_u32 = Storage_ps->slotStride_u32;

        s_FMKNVM_WriteU32(&Header_pu8[20U], headerCrc_u32);

        //---- 5- Select the slot following the active valid record ----//
        if(Runtime_ps->hasValidRecord_b == TRUE)
        {
            t_uint16 activeSlot_u16 = g_FMKNVM_Diagnostics_as[PartitionId_e].activeSlot_u16;

            g_FMKNVM_ActiveSlot_u16 = (t_uint16)((activeSlot_u16 + 1U) %
                           Runtime_ps->desc_ps->slotCount_u16);
        }
        else
        {
            g_FMKNVM_ActiveSlot_u16 = 0U;
        }

        //---- 6- Publish the physical candidate record boundaries ----//
        g_FMKNVM_ActiveSlotAddress_u32 = Storage_ps->storageOffset_u32 +
            ((t_uint32)g_FMKNVM_ActiveSlot_u16 *
             slotStride_u32);
        g_FMKNVM_ActiveRecordSize_u32 = FMKNVM_RECORD_HEADER_SIZE +
            Runtime_ps->dataSize_u32;
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitEraseState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitEraseState(void)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = g_FMKNVM_BackendApi_ps;
    void * BackendContext_pv = BackendApi_ps->context_pv;
    t_eFMKNVM_PartitionId PartitionId_e = g_FMKNVM_ActiveRequest_s.partitionId_e;
    t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[PartitionId_e];
    const t_sFMKNVM_PartitionStorageLayout * Storage_ps = &Runtime_ps->storage_s;
    t_uint32 slotStride_u32 = Storage_ps->slotStride_u32;
    t_eReturnCode Ret_e;

    //---- 1- Progress or start the candidate slot erase ----//
    if(g_FMKNVM_BackendOperationPending_b == TRUE)
    {
        Ret_e = s_FMKNVM_CommitWaitBackendState();
    }
    else
    {
        Ret_e = BackendApi_ps->Erase_pcb(BackendContext_pv, g_FMKNVM_ActiveSlotAddress_u32, slotStride_u32);
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitInvalidateMarkerState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitInvalidateMarkerState(void)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = g_FMKNVM_BackendApi_ps;
    void * BackendContext_pv = BackendApi_ps->context_pv;
    t_eFMKNVM_PartitionId PartitionId_e = g_FMKNVM_ActiveRequest_s.partitionId_e;
    t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[PartitionId_e];
    t_uint32 markerAddress_u32 = g_FMKNVM_ActiveSlotAddress_u32 +
        s_FMKNVM_GetMarkerOffset(Runtime_ps->desc_ps);
    t_eReturnCode Ret_e;

    //---- 1- Progress or start marker invalidation ----//
    if(g_FMKNVM_BackendOperationPending_b == TRUE)
    {
        Ret_e = s_FMKNVM_CommitWaitBackendState();
    }
    else
    {
        s_FMKNVM_WriteU32(g_FMKNVM_MarkerBuffer_au8, 0U);
        Ret_e = BackendApi_ps->Program_pcb(   BackendContext_pv,
                                              markerAddress_u32,
                                              g_FMKNVM_MarkerBuffer_au8,
                                              FMKNVM_RECORD_MARKER_SIZE);
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitProgramRecordState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitProgramRecordState(void)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = g_FMKNVM_BackendApi_ps;
    void * BackendContext_pv = BackendApi_ps->context_pv;
    t_eReturnCode Ret_e;

    //---- 1- Progress or start record programming ----//
    if(g_FMKNVM_BackendOperationPending_b == TRUE)
    {
        Ret_e = s_FMKNVM_CommitWaitBackendState();
    }
    else
    {
        Ret_e = BackendApi_ps->Program_pcb(   BackendContext_pv,
                                              g_FMKNVM_ActiveSlotAddress_u32,
                                              g_FMKNVM_RecordBuffer_au8,
                                              g_FMKNVM_ActiveRecordSize_u32);
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitReadRecordState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitReadRecordState(void)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = g_FMKNVM_BackendApi_ps;
    void * BackendContext_pv = BackendApi_ps->context_pv;
    t_eReturnCode Ret_e;

    //---- 1- Progress or start candidate record readback ----//
    if(g_FMKNVM_BackendOperationPending_b == TRUE)
    {
        Ret_e = s_FMKNVM_CommitWaitBackendState();
    }
    else
    {
        Ret_e = BackendApi_ps->Read_pcb(   BackendContext_pv,
                                           g_FMKNVM_ActiveSlotAddress_u32,
                                           g_FMKNVM_VerifyBuffer_au8,
                                           g_FMKNVM_ActiveRecordSize_u32);
    }

    if(Ret_e < RC_OK)
    {
        Ret_e = RC_ERROR_NVM_BACKEND_READ;
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitCheckRecordState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitCheckRecordState(void)
{
    t_eReturnCode Ret_e;

    //---- 1- Compare every programmed header and payload byte ----//
    Ret_e = SafeMem_memcmp(   g_FMKNVM_RecordBuffer_au8,
                              g_FMKNVM_VerifyBuffer_au8,
                              (t_uint16)g_FMKNVM_ActiveRecordSize_u32);

    if(Ret_e != RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
        Ret_e = RC_ERROR_NVM_NVM_VERIFY;
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitProgramMarkerState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitProgramMarkerState(void)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = g_FMKNVM_BackendApi_ps;
    void * BackendContext_pv = BackendApi_ps->context_pv;
    t_eFMKNVM_PartitionId PartitionId_e = g_FMKNVM_ActiveRequest_s.partitionId_e;
    t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[PartitionId_e];
    t_uint32 markerAddress_u32 = g_FMKNVM_ActiveSlotAddress_u32 +
        s_FMKNVM_GetMarkerOffset(Runtime_ps->desc_ps);
    t_eReturnCode Ret_e;

    //---- 1- Progress or start final marker programming ----//
    if(g_FMKNVM_BackendOperationPending_b == TRUE)
    {
        Ret_e = s_FMKNVM_CommitWaitBackendState();
    }
    else
    {
        s_FMKNVM_WriteU32(g_FMKNVM_MarkerBuffer_au8, FMKNVM_COMMIT_MARKER);
        Ret_e = BackendApi_ps->Program_pcb(   BackendContext_pv,
                                              markerAddress_u32,
                                              g_FMKNVM_MarkerBuffer_au8,
                                              FMKNVM_RECORD_MARKER_SIZE);
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitReadMarkerState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitReadMarkerState(void)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = g_FMKNVM_BackendApi_ps;
    void * BackendContext_pv = BackendApi_ps->context_pv;
    t_eFMKNVM_PartitionId PartitionId_e = g_FMKNVM_ActiveRequest_s.partitionId_e;
    t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[PartitionId_e];
    t_uint32 markerAddress_u32 = g_FMKNVM_ActiveSlotAddress_u32 +
        s_FMKNVM_GetMarkerOffset(Runtime_ps->desc_ps);
    t_eReturnCode Ret_e;

    //---- 1- Progress or start final marker readback ----//
    if(g_FMKNVM_BackendOperationPending_b == TRUE)
    {
        Ret_e = s_FMKNVM_CommitWaitBackendState();
    }
    else
    {
        Ret_e = BackendApi_ps->Read_pcb(   BackendContext_pv,
                                           markerAddress_u32,
                                           g_FMKNVM_VerifyBuffer_au8,
                                           FMKNVM_RECORD_MARKER_SIZE);
    }

    if(Ret_e < RC_OK)
    {
        Ret_e = RC_ERROR_NVM_BACKEND_READ;
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitCheckMarkerState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitCheckMarkerState(void)
{
    t_uint32 marker_u32;
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Deserialize the marker read from persistent storage ----//
    marker_u32 = s_FMKNVM_ReadU32(g_FMKNVM_VerifyBuffer_au8);

    //---- 2- Validate persistence before publishing success ----//
    if(marker_u32 != FMKNVM_COMMIT_MARKER)
    {
        Ret_e = RC_ERROR_NVM_NVM_VERIFY;
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitFinalizeState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitFinalizeState(void)
{
    t_eFMKNVM_PartitionId PartitionId_e = g_FMKNVM_ActiveRequest_s.partitionId_e;
    t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[PartitionId_e];
    t_uint32 currentTick_u32;
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Publish the committed generation in RAM ----//
    FMKCPU_GetTick(&currentTick_u32);
    Runtime_ps->committedGeneration_u32 = g_FMKNVM_SnapshotGeneration_u32;
    Runtime_ps->lastCommitTick_u32 = currentTick_u32;
    Runtime_ps->hasValidRecord_b = TRUE;
    Runtime_ps->commitPending_b = FALSE;
    Runtime_ps->dirty_b = Runtime_ps->currentGeneration_u32 != Runtime_ps->committedGeneration_u32;

    //---- 2- Publish the successful transaction diagnostics ----//
    g_FMKNVM_Diagnostics_as[PartitionId_e].successfulCommitCount_u32++;
    g_FMKNVM_Diagnostics_as[PartitionId_e].activeSlot_u16 = g_FMKNVM_ActiveSlot_u16;
    g_FMKNVM_Diagnostics_as[PartitionId_e].activeSequence_u32 = g_FMKNVM_SnapshotGeneration_u32;
    g_FMKNVM_Diagnostics_as[PartitionId_e].lastResult_e = RC_OK;

    //---- 3- Trace the completed persistent partition write ----//
    FMKSRL_LOG(   "[NVM] write partition=%u ret=%d size=%lu\r\n",
                  (t_uint16)PartitionId_e,
                  (t_sint16)Ret_e,
                  Runtime_ps->dataSize_u32);

    return Ret_e;
}

/*********************************
 * s_FMKNVM_CommitErrorState
 *********************************/
static t_eReturnCode s_FMKNVM_CommitErrorState(void)
{
    t_eFMKNVM_PartitionId PartitionId_e = g_FMKNVM_ActiveRequest_s.partitionId_e;
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the active request before cleanup ----//
    if(PartitionId_e < FMKNVM_PARTITION_NB)
    {
        t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[PartitionId_e];

        //---- 2- Preserve the modified cache for an automatic retry ----//
        Runtime_ps->commitPending_b = FALSE;
        Runtime_ps->dirty_b = TRUE;
        g_FMKNVM_Diagnostics_as[PartitionId_e].failedCommitCount_u32++;

        //---- 3- Trace the failed persistent partition write ----//
        t_eReturnCode WriteResult_e = g_FMKNVM_Diagnostics_as[PartitionId_e].lastResult_e;

        FMKSRL_LOG(   "[NVM] write partition=%u ret=%d size=%lu\r\n",
                      (t_uint16)PartitionId_e,
                      (t_sint16)WriteResult_e,
                      Runtime_ps->dataSize_u32);
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_RegisterCommitError
 *********************************/
static void s_FMKNVM_RegisterCommitError(t_eReturnCode f_Error_e)
{
    t_eFMKNVM_PartitionId PartitionId_e = g_FMKNVM_ActiveRequest_s.partitionId_e;

    //---- 1- Store the failure when an active object is available ----//
    if(PartitionId_e < FMKNVM_PARTITION_NB)
    {
        g_FMKNVM_Diagnostics_as[PartitionId_e].lastResult_e = f_Error_e;
    }

    //---- 2- Report every failed commit transaction ----//
    ASSERT((t_sint32)f_Error_e);
}

/*********************************
 * s_FMKNVM_OperationalState
 *********************************/
static t_eReturnCode s_FMKNVM_OperationalState(void)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Service optional backend asynchronous work ----//
    if(g_FMKNVM_BackendApi_ps->Cyclic_pcb != NULL)
    {
        g_FMKNVM_BackendApi_ps->Cyclic_pcb(g_FMKNVM_BackendApi_ps->context_pv);
    }

    //---- 2- Schedule one automatic commit request when required ----//
    Ret_e = s_FMKNVM_AutomaticCommitMngmt();

    if((Ret_e == RC_WARNING_NO_OPERATION) || (Ret_e == RC_WARNING_PENDING) || (Ret_e == RC_WARNING_BUSY))
    {
        Ret_e = RC_OK;
    }

    //---- 3- Progress one bounded step of the commit transaction ----//
    if(Ret_e == RC_OK)
    {
        Ret_e = s_FMKNVM_ProcessCommit();
    }

    //---- 4- Report an operational scheduling or commit error ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_WriteU32
 *********************************/
static void s_FMKNVM_WriteU32(t_uint8 * f_Data_pu8, t_uint32 f_Value_u32)
{
    //---- 1- Serialize the value in the persistent byte order ----//
    f_Data_pu8[0U] = (t_uint8)(f_Value_u32 & 0xFFUL);
    f_Data_pu8[1U] = (t_uint8)((f_Value_u32 >> 8U) & 0xFFUL);
    f_Data_pu8[2U] = (t_uint8)((f_Value_u32 >> 16U) & 0xFFUL);
    f_Data_pu8[3U] = (t_uint8)((f_Value_u32 >> 24U) & 0xFFUL);
}

/*********************************
 * s_FMKNVM_WriteU16
 *********************************/
static void s_FMKNVM_WriteU16(t_uint8 * f_Data_pu8, t_uint16 f_Value_u16)
{
    //---- 1- Serialize the value in the persistent byte order ----//
    f_Data_pu8[0U] = (t_uint8)(f_Value_u16 & 0xFFU);
    f_Data_pu8[1U] = (t_uint8)((f_Value_u16 >> 8U) & 0xFFU);
}

/*********************************
 * s_FMKNVM_ReadU32
 *********************************/
static t_uint32 s_FMKNVM_ReadU32(const t_uint8 * f_Data_pcu8)
{
    t_uint32 Value_u32;

    //---- 1- Deserialize the persistent little-endian value ----//
    Value_u32 = ((t_uint32)f_Data_pcu8[0U]) |
        ((t_uint32)f_Data_pcu8[1U] << 8U) |
        ((t_uint32)f_Data_pcu8[2U] << 16U) |
        ((t_uint32)f_Data_pcu8[3U] << 24U);

    return Value_u32;
}

/*********************************
 * s_FMKNVM_ReadU16
 *********************************/
static t_uint16 s_FMKNVM_ReadU16(const t_uint8 * f_Data_pcu8)
{
    t_uint16 Value_u16;

    //---- 1- Deserialize the persistent little-endian value ----//
    Value_u16 = (t_uint16)(
            ((t_uint16)f_Data_pcu8[0U]) |
            ((t_uint16)f_Data_pcu8[1U] << 8U));

    return Value_u16;
}

/*********************************
 * s_FMKNVM_GetMarkerOffset
 *********************************/
static t_uint32 s_FMKNVM_GetMarkerOffset(   const t_sFMKNVM_PartitionDescriptor * f_Descriptor_ps)
{
    t_uint32 MarkerOffset_u32 = FMKNVM_RECORD_HEADER_SIZE +
        f_Descriptor_ps->payloadCapacity_u32;
    t_uint32 CommitUnitSize_u32 = g_FMKNVM_Geometry_s.commitUnitSize_u32;
    t_uint32 AlignmentRemainder_u32 = MarkerOffset_u32 % CommitUnitSize_u32;

    //---- 1- Move the marker to the next commit-unit boundary ----//
    if(AlignmentRemainder_u32 != 0U)
    {
        MarkerOffset_u32 += CommitUnitSize_u32 - AlignmentRemainder_u32;
    }

    return MarkerOffset_u32;
}

/*********************************
 * s_FMKNVM_AlignUp
 *********************************/
static t_eReturnCode s_FMKNVM_AlignUp(t_uint32 f_Value_u32, t_uint32 f_Alignment_u32,
                                      t_uint32 * f_AlignedValue_pu32)
{
    t_eReturnCode Ret_e = RC_OK;

    if(f_AlignedValue_pu32 == NULL)
    {
        ASSERT((t_sint32)0);
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_Alignment_u32 == 0U)
    {
        ASSERT((t_sint32)0);
        Ret_e = RC_ERROR_WRONG_CONFIG;
    }
    else
    {
        t_uint32 remainder_u32 = f_Value_u32 % f_Alignment_u32;

        if(remainder_u32 == 0U)
        {
            *f_AlignedValue_pu32 = f_Value_u32;
        }
        else
        {
            t_uint32 increment_u32 = f_Alignment_u32 - remainder_u32;

            if(f_Value_u32 > (0xFFFFFFFFUL - increment_u32))
            {
                ASSERT((t_sint32)0);
                Ret_e = RC_ERROR_WRONG_CONFIG;
            }
            else
            {
                *f_AlignedValue_pu32 = f_Value_u32 + increment_u32;
            }
        }
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_ComputeStorageLayout
 *********************************/
static t_eReturnCode s_FMKNVM_ComputeStorageLayout(void)
{
    t_eReturnCode Ret_e = RC_OK;
    t_uint32 slotAlignment_u32 = 0U;
    t_uint32 nextPartitionOffset_u32 = 0U;

    //---- 1- Validate geometry required by the layout calculation ----//
    if((g_FMKNVM_Geometry_s.storageSize_u32 == 0U) ||
       (g_FMKNVM_Geometry_s.programUnitSize_u32 == 0U) ||
       (g_FMKNVM_Geometry_s.commitUnitSize_u32 == 0U))
    {
        ASSERT((t_sint32)0);
        Ret_e = RC_ERROR_WRONG_CONFIG;
    }
    else if(g_FMKNVM_Capabilities_s.eraseRequired_b == TRUE)
    {
        //---- One Flash slot must own complete independent erase units ----//
        if((g_FMKNVM_Geometry_s.eraseUnitSize_u32 == 0U) ||
           ((g_FMKNVM_Geometry_s.eraseUnitSize_u32 % g_FMKNVM_Geometry_s.programUnitSize_u32) != 0U) ||
           ((g_FMKNVM_Geometry_s.eraseUnitSize_u32 % g_FMKNVM_Geometry_s.commitUnitSize_u32) != 0U))
        {
            ASSERT((t_sint32)0);
            Ret_e = RC_ERROR_WRONG_CONFIG;
        }
        else
        {
            slotAlignment_u32 = g_FMKNVM_Geometry_s.eraseUnitSize_u32;
        }
    }
    else
    {
        //---- EEPROM slots are packed on the LCM of program and commit units ----//
        t_uint32 gcdA_u32 = g_FMKNVM_Geometry_s.programUnitSize_u32;
        t_uint32 gcdB_u32 = g_FMKNVM_Geometry_s.commitUnitSize_u32;

        while(gcdB_u32 != 0U)
        {
            t_uint32 remainder_u32 = gcdA_u32 % gcdB_u32;
            gcdA_u32 = gcdB_u32;
            gcdB_u32 = remainder_u32;
        }

        if((gcdA_u32 == 0U) ||
           ((g_FMKNVM_Geometry_s.programUnitSize_u32 / gcdA_u32) >
            (0xFFFFFFFFUL / g_FMKNVM_Geometry_s.commitUnitSize_u32)))
        {
            ASSERT((t_sint32)0);
            Ret_e = RC_ERROR_WRONG_CONFIG;
        }
        else
        {
            slotAlignment_u32 = (g_FMKNVM_Geometry_s.programUnitSize_u32 / gcdA_u32) *
                g_FMKNVM_Geometry_s.commitUnitSize_u32;
        }
    }

    //---- 2- Allocate partitions sequentially in enum order ----//
    for(t_uint16 idx_u16 = 0U ;
        (idx_u16 < (t_uint16)FMKNVM_PARTITION_NB) && (Ret_e == RC_OK) ;
        idx_u16++)
    {
        t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[idx_u16];
        const t_sFMKNVM_PartitionDescriptor * Descriptor_ps = Runtime_ps->desc_ps;

        if((Descriptor_ps == NULL) || (Descriptor_ps->cacheData_pu8 == NULL) ||
           (Descriptor_ps->payloadCapacity_u32 == 0U) ||
           (Descriptor_ps->payloadCapacity_u32 > FMKNVM_MAX_PARTITION_PAYLOAD_CAPACITY) ||
           (Descriptor_ps->payloadCapacity_u32 > 0xFFFFU) ||
           (Descriptor_ps->formatVersion_u16 == 0U) ||
           (Descriptor_ps->slotCount_u16 < 2U))
        {
            ASSERT((t_sint32)0);
            Ret_e = RC_ERROR_WRONG_CONFIG;
        }
        else
        {
            t_uint32 requiredSlotSize_u32 = s_FMKNVM_GetMarkerOffset(Descriptor_ps);
            t_uint32 slotStride_u32 = 0U;
            t_uint32 partitionOffset_u32 = 0U;
            t_uint32 partitionSize_u32 = 0U;

            if(requiredSlotSize_u32 > (0xFFFFFFFFUL - FMKNVM_RECORD_MARKER_SIZE))
            {
                ASSERT((t_sint32)0);
                Ret_e = RC_ERROR_WRONG_CONFIG;
            }
            else
            {
                requiredSlotSize_u32 += FMKNVM_RECORD_MARKER_SIZE;
                Ret_e = s_FMKNVM_AlignUp(requiredSlotSize_u32, slotAlignment_u32, &slotStride_u32);
            }

            if(Ret_e == RC_OK)
            {
                Ret_e = s_FMKNVM_AlignUp(nextPartitionOffset_u32, slotAlignment_u32, &partitionOffset_u32);
            }

            if(Ret_e == RC_OK)
            {
                if(slotStride_u32 > (0xFFFFFFFFUL / (t_uint32)Descriptor_ps->slotCount_u16))
                {
                    Ret_e = RC_ERROR_WRONG_CONFIG;
                }
                else
                {
                    partitionSize_u32 = slotStride_u32 * (t_uint32)Descriptor_ps->slotCount_u16;
                }
            }

            //---- 3- Reject the partition before publishing a range outside the backend ----//
            if(Ret_e == RC_OK)
            {
                if((partitionOffset_u32 > g_FMKNVM_Geometry_s.storageSize_u32) ||
                   (partitionSize_u32 > (g_FMKNVM_Geometry_s.storageSize_u32 - partitionOffset_u32)))
                {
                    Ret_e = RC_ERROR_WRONG_CONFIG;
                }
                else
                {
                    Runtime_ps->storage_s.storageOffset_u32 = partitionOffset_u32;
                    Runtime_ps->storage_s.storageSize_u32 = partitionSize_u32;
                    Runtime_ps->storage_s.slotStride_u32 = slotStride_u32;
                    nextPartitionOffset_u32 = partitionOffset_u32 + partitionSize_u32;
                }
            }
        }
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_ValidateConfiguration
 *********************************/
static t_eReturnCode s_FMKNVM_ValidateConfiguration(void)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the generic backend geometry ----//
    if((g_FMKNVM_Geometry_s.storageSize_u32 == 0U) ||
       (g_FMKNVM_Geometry_s.programUnitSize_u32 == 0U) ||
       (g_FMKNVM_Geometry_s.commitUnitSize_u32 == 0U) ||
       ((g_FMKNVM_Capabilities_s.eraseRequired_b == TRUE) &&
        (g_FMKNVM_Geometry_s.eraseUnitSize_u32 == 0U)))
    {
        ASSERT((t_sint32)0);
        Ret_e = RC_ERROR_WRONG_CONFIG;
    }

    //---- 2- Validate every derived partition range and slot stride ----//
    for(t_uint16 idx_u16 = 0U ;
        (idx_u16 < (t_uint16)FMKNVM_PARTITION_NB) && (Ret_e == RC_OK) ;
        idx_u16++)
    {
        t_sFMKNVM_PartitionRuntime * Runtime_ps = &g_FMKNVM_Runtime_as[idx_u16];
        const t_sFMKNVM_PartitionDescriptor * Descriptor_ps = Runtime_ps->desc_ps;
        const t_sFMKNVM_PartitionStorageLayout * Storage_ps = &Runtime_ps->storage_s;

        if((Descriptor_ps == NULL) || (Descriptor_ps->cacheData_pu8 == NULL) ||
           (Descriptor_ps->slotCount_u16 < 2U) ||
           (Descriptor_ps->payloadCapacity_u32 == 0U) ||
           (Descriptor_ps->payloadCapacity_u32 > FMKNVM_MAX_PARTITION_PAYLOAD_CAPACITY) ||
           (Descriptor_ps->payloadCapacity_u32 > 0xFFFFU) ||
           (Descriptor_ps->formatVersion_u16 == 0U) ||
           (Storage_ps->storageSize_u32 == 0U) ||
           (Storage_ps->slotStride_u32 == 0U))
        {
            ASSERT((t_sint32)0);
            Ret_e = RC_ERROR_WRONG_CONFIG;
        }
        else
        {
            t_uint32 partitionEnd_u32 = Storage_ps->storageOffset_u32 + Storage_ps->storageSize_u32;
            t_uint32 requiredSlotSize_u32 = s_FMKNVM_GetMarkerOffset(Descriptor_ps) + FMKNVM_RECORD_MARKER_SIZE;
            t_uint32 expectedStorageSize_u32 = 0U;

            if(Storage_ps->slotStride_u32 > (0xFFFFFFFFUL / (t_uint32)Descriptor_ps->slotCount_u16))
            {
                ASSERT((t_sint32)0);
                Ret_e = RC_ERROR_WRONG_CONFIG;
            }
            else
            {
                expectedStorageSize_u32 = Storage_ps->slotStride_u32 * (t_uint32)Descriptor_ps->slotCount_u16;
            }

            if((Ret_e == RC_OK) &&
               ((partitionEnd_u32 < Storage_ps->storageOffset_u32) ||
                (partitionEnd_u32 > g_FMKNVM_Geometry_s.storageSize_u32) ||
                (Storage_ps->storageSize_u32 != expectedStorageSize_u32) ||
                (Storage_ps->slotStride_u32 < requiredSlotSize_u32) ||
                ((Storage_ps->storageOffset_u32 % g_FMKNVM_Geometry_s.programUnitSize_u32) != 0U) ||
                ((Storage_ps->slotStride_u32 % g_FMKNVM_Geometry_s.programUnitSize_u32) != 0U) ||
                ((Storage_ps->storageOffset_u32 % g_FMKNVM_Geometry_s.commitUnitSize_u32) != 0U) ||
                ((Storage_ps->slotStride_u32 % g_FMKNVM_Geometry_s.commitUnitSize_u32) != 0U)))
            {
                ASSERT((t_sint32)0);
                Ret_e = RC_ERROR_WRONG_CONFIG;
            }
            else if((Ret_e == RC_OK) && (g_FMKNVM_Capabilities_s.eraseRequired_b == TRUE))
            {
                if(((Storage_ps->storageOffset_u32 % g_FMKNVM_Geometry_s.eraseUnitSize_u32) != 0U) ||
                   ((Storage_ps->slotStride_u32 % g_FMKNVM_Geometry_s.eraseUnitSize_u32) != 0U))
                {
                    ASSERT((t_sint32)0);
                    Ret_e = RC_ERROR_WRONG_CONFIG;
                }
            }
            else
            {
                //---- No additional erase-unit constraint applies to EEPROM backends ----//
            }
        }
    }

    //---- 3- Reject overlapping derived partition ranges ----//
    for(t_uint16 firstIdx_u16 = 0U ;
        (firstIdx_u16 < (t_uint16)FMKNVM_PARTITION_NB) && (Ret_e == RC_OK) ;
        firstIdx_u16++)
    {
        const t_sFMKNVM_PartitionStorageLayout * FirstStorage_ps =
            &g_FMKNVM_Runtime_as[firstIdx_u16].storage_s;
        t_uint32 firstEnd_u32 = FirstStorage_ps->storageOffset_u32 + FirstStorage_ps->storageSize_u32;

        for(t_uint16 secondIdx_u16 = firstIdx_u16 + 1U ;
            (secondIdx_u16 < (t_uint16)FMKNVM_PARTITION_NB) && (Ret_e == RC_OK) ;
            secondIdx_u16++)
        {
            const t_sFMKNVM_PartitionStorageLayout * SecondStorage_ps =
                &g_FMKNVM_Runtime_as[secondIdx_u16].storage_s;
            t_uint32 secondEnd_u32 = SecondStorage_ps->storageOffset_u32 + SecondStorage_ps->storageSize_u32;
            t_bool rangesOverlap_b = (FirstStorage_ps->storageOffset_u32 < secondEnd_u32) &&
                (SecondStorage_ps->storageOffset_u32 < firstEnd_u32);

            if(rangesOverlap_b == TRUE)
            {
                ASSERT((t_sint32)0);
                Ret_e = RC_ERROR_WRONG_CONFIG;
            }
        }
    }

    //---- 4- Validate every logical object mapping ----//
    for(t_uint16 idx_u16 = 0U ;
        (idx_u16 < (t_uint16)FMKNVM_OBJECT_NB) && (Ret_e == RC_OK) ;
        idx_u16++)
    {
        const t_sFMKNVM_ObjectDescriptor * ObjectDesc_ps = &c_FMKNVM_ObjectDescriptors_as[idx_u16];

        if((ObjectDesc_ps->partitionId_e >= FMKNVM_PARTITION_NB) ||
           (ObjectDesc_ps->size_u32 == 0U) ||
           (ObjectDesc_ps->version_u16 == 0U))
        {
            ASSERT((t_sint32)0);
            Ret_e = RC_ERROR_WRONG_CONFIG;
        }
        else
        {
            const t_sFMKNVM_PartitionDescriptor * PartitionDesc_ps =
                &c_FMKNVM_PartitionDescriptors_as[ObjectDesc_ps->partitionId_e];
            t_uint32 objectEnd_u32 = ObjectDesc_ps->offset_u32 + ObjectDesc_ps->size_u32;

            if((ObjectDesc_ps->offset_u32 < FMKNVM_OBJECT_VALIDITY_BITMAP_SIZE) ||
               (objectEnd_u32 > PartitionDesc_ps->payloadCapacity_u32) ||
               (objectEnd_u32 < ObjectDesc_ps->offset_u32))
            {
                ASSERT((t_sint32)0);
                Ret_e = RC_ERROR_WRONG_CONFIG;
            }
        }
    }

    //---- 5- Reject overlapping objects inside the same partition ----//
    for(t_uint16 firstIdx_u16 = 0U ;
        (firstIdx_u16 < (t_uint16)FMKNVM_OBJECT_NB) && (Ret_e == RC_OK) ;
        firstIdx_u16++)
    {
        const t_sFMKNVM_ObjectDescriptor * FirstObject_ps = &c_FMKNVM_ObjectDescriptors_as[firstIdx_u16];
        t_uint32 firstEnd_u32 = FirstObject_ps->offset_u32 + FirstObject_ps->size_u32;

        for(t_uint16 secondIdx_u16 = firstIdx_u16 + 1U ;
            (secondIdx_u16 < (t_uint16)FMKNVM_OBJECT_NB) && (Ret_e == RC_OK) ;
            secondIdx_u16++)
        {
            const t_sFMKNVM_ObjectDescriptor * SecondObject_ps = &c_FMKNVM_ObjectDescriptors_as[secondIdx_u16];
            t_uint32 secondEnd_u32 = SecondObject_ps->offset_u32 + SecondObject_ps->size_u32;
            t_bool rangesOverlap_b = (FirstObject_ps->offset_u32 < secondEnd_u32) &&
                (SecondObject_ps->offset_u32 < firstEnd_u32);

            if((FirstObject_ps->partitionId_e == SecondObject_ps->partitionId_e) &&
               (rangesOverlap_b == TRUE))
            {
                ASSERT((t_sint32)0);
                Ret_e = RC_ERROR_WRONG_CONFIG;
            }
        }
    }

    //---- 6- Report an invalid persistent storage configuration ----//
    if(Ret_e < RC_OK)
    {
        ASSERT((t_sint32)Ret_e);
    }

    return Ret_e;
}
