/*********************************************************************
 * @file        FMKNVM_Backend.h
 * @brief       Private hardware-independent backend contract for FMK_NVM.
 *********************************************************************/
#ifndef FMKNVM_BACKEND_H_INCLUDED
#define FMKNVM_BACKEND_H_INCLUDED

#include "TypeCommon.h"

/// @brief Describes generic physical storage geometry.
typedef struct __t_sFMKNVM_BackendGeometry
{
    t_uint32 storageSize_u32;     ///< Total usable size in bytes.
    t_uint32 eraseUnitSize_u32;   ///< Smallest erasable unit in bytes.
    t_uint32 programUnitSize_u32; ///< Smallest program unit in bytes.
    t_uint32 commitUnitSize_u32;  ///< Atomic commit unit in bytes.
} t_sFMKNVM_BackendGeometry;

/// @brief Describes generic backend operation capabilities.
typedef struct __t_sFMKNVM_BackendCapabilities
{
    t_bool eraseRequired_b;           ///< Erase is required before program.
    t_bool readWhileWriteSupported_b; ///< Read is permitted during writes.
    t_uint32 maxTransferSize_u32;     ///< Maximum transfer size in bytes.
} t_sFMKNVM_BackendCapabilities;

/**
 * @brief Initialise one backend instance.
 * @note The backend prepares only its own resource.It must not retain data
 *       buffers supplied to Read or Program.
 * @param[in] f_Context_pv : Opaque backend context from t_sFMKNVM_BackendApi.
 * @retval RC_OK Success.
 * @retval RC_ERROR_MISSING_CONFIG Backend configuration is incomplete.
 */
typedef t_eReturnCode (t_cbFMKNVM_BackendInit)(void * f_Context_pv);
/**
 * @brief Read generic geometry from one backend instance.
 * @param[in] f_Context_pv : Opaque backend context.
 * @param[out] f_Geometry_ps : Geometry populated by the backend.
 * @retval RC_OK Success.
 * @retval RC_ERROR_PTR_NULL Output pointer is null.
 */
typedef t_eReturnCode
    (t_cbFMKNVM_BackendGetGeometry)(   void * f_Context_pv,
                                       t_sFMKNVM_BackendGeometry *
                                       f_Geometry_ps);
/**
 * @brief Read generic capabilities from one backend instance.
 * @param[in] f_Context_pv : Opaque backend context.
 * @param[out] f_Capabilities_ps : Capabilities populated by the backend.
 * @retval RC_OK Success.
 * @retval RC_ERROR_PTR_NULL Output pointer is null.
 */
typedef t_eReturnCode
    (t_cbFMKNVM_BackendGetCapabilities)(   void * f_Context_pv,
                                           t_sFMKNVM_BackendCapabilities *
                                           f_Capabilities_ps);
/**
 * @brief Read bytes from backend storage into a caller-owned buffer.
 * @note The buffer belongs to FMK_NVM and is valid only for this call.
 * @param[in] f_Context_pv : Opaque backend context.
 * @param[in] f_Address_u32 : Backend-relative source address in bytes.
 * @param[out] f_Data_pu8 : Destination buffer of f_Size_u32 bytes.
 * @param[in] f_Size_u32 : Number of bytes to read.
 * @retval RC_OK Success.
 * @retval RC_NVM_BACKEND_READ_ERROR Physical read failed.
 */
typedef t_eReturnCode (t_cbFMKNVM_BackendRead)(   void * f_Context_pv,
                                                  t_uint32 f_Address_u32,
                                                  t_uint8 * f_Data_pu8,
                                                  t_uint32 f_Size_u32);
/**
 * @brief Program an already erased backend range from a caller-owned buffer.
 * @note The backend shall consume the bytes before returning, or copy them
 *       internally before starting an asynchronous transfer.
 * @param[in] f_Context_pv : Opaque backend context.
 * @param[in] f_Address_u32 : Backend-relative destination address in bytes.
 * @param[in] f_Data_pcu8 : Source buffer of f_Size_u32 bytes.
 * @param[in] f_Size_u32 : Number of bytes to program.
 * @retval RC_OK Success or operation accepted.
 * @retval RC_NVM_BACKEND_PROGRAM_ERROR Physical program failed.
 */
typedef t_eReturnCode
    (t_cbFMKNVM_BackendProgram)(   void * f_Context_pv,
                                   t_uint32 f_Address_u32,
                                   const t_uint8 * f_Data_pcu8,
                                   t_uint32 f_Size_u32);
/**
 * @brief Erase an aligned backend range before programming when required.
 * @param[in] f_Context_pv : Opaque backend context.
 * @param[in] f_Address_u32 : Backend-relative, erase-unit aligned address.
 * @param[in] f_Size_u32 : Erase-unit multiple to erase in bytes.
 * @retval RC_OK Success or operation accepted.
 * @retval RC_NVM_BACKEND_ERASE_ERROR Physical erase failed.
 */
typedef t_eReturnCode (t_cbFMKNVM_BackendErase)(void * f_Context_pv, t_uint32 f_Address_u32, t_uint32 f_Size_u32);
/**
 * @brief Report whether a backend asynchronous operation is active.
 * @param[in] f_Context_pv : Opaque backend context.
 * @retval TRUE An operation remains pending.
 * @retval FALSE No operation is pending.
 */
typedef t_bool (t_cbFMKNVM_BackendIsBusy)(void * f_Context_pv);
/**
 * @brief Retrieve the result of a completed backend operation.
 * @param[in] f_Context_pv : Opaque backend context.
 * @param[out] f_OperationResult_pe : Final result of the backend operation.
 * @retval RC_OK Completion result is available.
 * @retval RC_WARNING_PENDING The operation is not complete.
 */
typedef t_eReturnCode
    (t_cbFMKNVM_BackendGetOperationResult)(   void * f_Context_pv,
                                              t_eReturnCode *
                                              f_OperationResult_pe);
/**
 * @brief Progress backend work that requires cyclic servicing.
 * @param[in] f_Context_pv : Opaque backend context.
 * @note No buffer is supplied and no callback to FMK_NVM is permitted here.
 */
typedef void (t_cbFMKNVM_BackendCyclic)(void * f_Context_pv);

/// @brief Holds the private operations of one selected NVM backend.
typedef struct __t_sFMKNVM_BackendApi
{
    void * context_pv;                                              ///< Backend-owned opaque context.
    t_cbFMKNVM_BackendInit * Init_pcb;                              ///< Initialisation callback.
    t_cbFMKNVM_BackendGetGeometry * GetGeometry_pcb;                ///< Geometry retrieval callback.
    t_cbFMKNVM_BackendGetCapabilities * GetCapabilities_pcb;        ///< Capability retrieval callback.
    t_cbFMKNVM_BackendRead * Read_pcb;                              ///< Byte read callback.
    t_cbFMKNVM_BackendProgram * Program_pcb;                        ///< Byte program callback.
    t_cbFMKNVM_BackendErase * Erase_pcb;                            ///< Erase callback.
    t_cbFMKNVM_BackendIsBusy * IsBusy_pcb;                          ///< Busy query callback.
    t_cbFMKNVM_BackendGetOperationResult * GetOperationResult_pcb;  ///< Completion result callback.
    t_cbFMKNVM_BackendCyclic * Cyclic_pcb;                          ///< Backend cyclic callback.
} t_sFMKNVM_BackendApi;

#endif // FMKNVM_BACKEND_H_INCLUDED
