/*********************************************************************
 * @file        FMKNVM_BackendTemplate.c
 * @brief       Generated concrete backend implementation for FMK_NVM.
 * @note        Replace BackendTemplate and every generated section.
 *********************************************************************/
#include "FMK_CFG/FMKCFG_ConfigSpecific/FmkNvmConfigSpecific/FMKNVM_BackendTemplate.h"

/// @brief Complete addressable capacity of the generated backend.
#define FMKNVM_TEMPLATE_STORAGE_SIZE ((t_uint32)0U)

/// @brief Smallest independently erasable range, or zero without erase.
#define FMKNVM_TEMPLATE_ERASE_UNIT_SIZE ((t_uint32)0U)

/// @brief Smallest independently programmable unit.
#define FMKNVM_TEMPLATE_PROGRAM_UNIT_SIZE ((t_uint32)1U)

/// @brief Atomic unit used to publish the final commit marker.
#define FMKNVM_TEMPLATE_COMMIT_UNIT_SIZE ((t_uint32)1U)

/// @brief Private runtime state of the generated concrete backend.
typedef struct __t_sFMKNVM_BackendTemplateContext
{
    t_eReturnCode lastOperationResult_e;  ///< Latest backend result.
    t_bool isInitialized_b;               ///< Backend initialization status.
    t_bool isBusy_b;                      ///< Asynchronous operation status.
} t_sFMKNVM_BackendTemplateContext;

/// @brief Lifetime-stable private context of the generated backend.
static t_sFMKNVM_BackendTemplateContext g_FMKNVM_BackendTemplateContext_s;

/**
 * @brief Initialize the generated backend and its private context.
 * @note No caller-owned pointer is retained.Hardware discovery or an
 *       asynchronous initialization may return RC_WARNING_PENDING.
 *
 * @param[in] f_Context_pv : Backend-owned context valid for module lifetime.
 *
 * @retval RC_OK                               @ref RC_OK
 * @retval RC_WARNING_PENDING                  @ref RC_WARNING_PENDING
 * @retval RC_ERROR_PTR_NULL                   @ref RC_ERROR_PTR_NULL
 * @retval RC_ERROR_WRONG_CONFIG               @ref RC_ERROR_WRONG_CONFIG
 */
static t_eReturnCode s_FMKNVM_TemplateInit(void * f_Context_pv);

/**
 * @brief Return the generic geometry of the generated backend.
 * @note Geometry values describe the complete address range exposed to
 *       FMK_NVM and remain constant after initialization.
 *
 * @param[in]  f_Context_pv : Initialized backend-owned context.
 * @param[out] f_Geometry_ps : Caller-owned generic geometry destination.
 *
 * @retval RC_OK                             @ref RC_OK
 * @retval RC_ERROR_PTR_NULL                 @ref RC_ERROR_PTR_NULL
 * @retval RC_ERROR_INSTANCE_NOT_INITIALIZED @ref RC_ERROR_INSTANCE_NOT_INITIALIZED
 */
static t_eReturnCode s_FMKNVM_TemplateGetGeometry(void * f_Context_pv, t_sFMKNVM_BackendGeometry * f_Geometry_ps);

/**
 * @brief Return the operational capabilities of the generated backend.
 * @note The output indicates erase requirements, concurrency support and the
 *       maximum accepted transfer size.
 *
 * @param[in]  f_Context_pv : Initialized backend-owned context.
 * @param[out] f_Capabilities_ps : Caller-owned capability destination.
 *
 * @retval RC_OK                             @ref RC_OK
 * @retval RC_ERROR_PTR_NULL                 @ref RC_ERROR_PTR_NULL
 * @retval RC_ERROR_INSTANCE_NOT_INITIALIZED @ref RC_ERROR_INSTANCE_NOT_INITIALIZED
 */
static t_eReturnCode s_FMKNVM_TemplateGetCapabilities(   void * f_Context_pv,
                                                         t_sFMKNVM_BackendCapabilities * f_Capabilities_ps);

/**
 * @brief Read bytes from the generated persistent device.
 * @note The destination is used only during this call and is never retained.
 *       An asynchronous backend copies through backend-owned storage.
 *
 * @param[in]  f_Context_pv : Initialized backend-owned context.
 * @param[in]  f_Address_u32 : Backend-relative first byte address.
 * @param[out] f_Data_pu8 : Caller-owned destination receiving the bytes.
 * @param[in]  f_Size_u32 : Non-zero number of bytes to read.
 *
 * @retval RC_OK                             @ref RC_OK
 * @retval RC_WARNING_PENDING                @ref RC_WARNING_PENDING
 * @retval RC_ERROR_PTR_NULL                 @ref RC_ERROR_PTR_NULL
 * @retval RC_ERROR_PARAM_INVALID            @ref RC_ERROR_PARAM_INVALID
 * @retval RC_ERROR_INSTANCE_NOT_INITIALIZED @ref RC_ERROR_INSTANCE_NOT_INITIALIZED
 * @retval RC_NVM_BACKEND_READ_ERROR         @ref RC_NVM_BACKEND_READ_ERROR
 */
static t_eReturnCode s_FMKNVM_TemplateRead(   void * f_Context_pv,
                                              t_uint32 f_Address_u32,
                                              t_uint8 * f_Data_pu8,
                                              t_uint32 f_Size_u32);

/**
 * @brief Program bytes into the generated persistent device.
 * @note The source is consumed or copied during this call and never retained.
 *       Address and size must respect the advertised program unit.
 *
 * @param[in] f_Context_pv : Initialized backend-owned context.
 * @param[in] f_Address_u32 : Backend-relative first byte address.
 * @param[in] f_Data_pcu8 : Caller-owned source bytes.
 * @param[in] f_Size_u32 : Non-zero number of bytes to program.
 *
 * @retval RC_OK                                @ref RC_OK
 * @retval RC_WARNING_PENDING                   @ref RC_WARNING_PENDING
 * @retval RC_ERROR_PTR_NULL                    @ref RC_ERROR_PTR_NULL
 * @retval RC_ERROR_PARAM_INVALID               @ref RC_ERROR_PARAM_INVALID
 * @retval RC_ERROR_INSTANCE_NOT_INITIALIZED    @ref RC_ERROR_INSTANCE_NOT_INITIALIZED
 * @retval RC_NVM_BACKEND_PROGRAM_ERROR         @ref RC_NVM_BACKEND_PROGRAM_ERROR
 */
static t_eReturnCode s_FMKNVM_TemplateProgram(   void * f_Context_pv,
                                                 t_uint32 f_Address_u32,
                                                 const t_uint8 * f_Data_pcu8,
                                                 t_uint32 f_Size_u32);

/**
 * @brief Erase an aligned range of the generated persistent device.
 * @note EEPROM implementations without an erase requirement return RC_OK
 *       without accessing the physical device.
 *
 * @param[in] f_Context_pv : Initialized backend-owned context.
 * @param[in] f_Address_u32 : Backend-relative aligned start address.
 * @param[in] f_Size_u32 : Non-zero aligned range to erase.
 *
 * @retval RC_OK                              @ref RC_OK
 * @retval RC_WARNING_PENDING                 @ref RC_WARNING_PENDING
 * @retval RC_ERROR_PTR_NULL                  @ref RC_ERROR_PTR_NULL
 * @retval RC_ERROR_PARAM_INVALID             @ref RC_ERROR_PARAM_INVALID
 * @retval RC_ERROR_INSTANCE_NOT_INITIALIZED  @ref RC_ERROR_INSTANCE_NOT_INITIALIZED
 * @retval RC_NVM_BACKEND_ERASE_ERROR         @ref RC_NVM_BACKEND_ERASE_ERROR
 */
static t_eReturnCode s_FMKNVM_TemplateErase(void * f_Context_pv, t_uint32 f_Address_u32, t_uint32 f_Size_u32);

/**
 * @brief Report whether an asynchronous backend operation is active.
 *
 * @param[in] f_Context_pv : Backend-owned context valid for module lifetime.
 *
 * @retval TRUE  A backend operation remains active.
 * @retval FALSE No backend operation is active.
 */
static t_bool s_FMKNVM_TemplateIsBusy(void * f_Context_pv);

/**
 * @brief Return the result of the latest completed backend operation.
 *
 * @param[in]  f_Context_pv : Initialized backend-owned context.
 * @param[out] f_OperationResult_pe : Caller-owned result destination.
 *
 * @retval RC_OK                             @ref RC_OK
 * @retval RC_ERROR_PTR_NULL                 @ref RC_ERROR_PTR_NULL
 * @retval RC_ERROR_INSTANCE_NOT_INITIALIZED @ref RC_ERROR_INSTANCE_NOT_INITIALIZED
 */
static t_eReturnCode s_FMKNVM_TemplateGetOperationResult(void * f_Context_pv, t_eReturnCode * f_OperationResult_pe);

/**
 * @brief Progress asynchronous work owned by the generated backend.
 * @note The function performs one bounded step and never blocks for the
 *       complete duration of a physical transaction.
 *
 * @param[in] f_Context_pv : Backend-owned context valid for module lifetime.
 */
static void s_FMKNVM_TemplateCyclic(void * f_Context_pv);

/// @brief Immutable callback table of the generated concrete backend.
static const t_sFMKNVM_BackendApi c_FMKNVM_BackendTemplateApi_s =
{
    .context_pv = &g_FMKNVM_BackendTemplateContext_s,
    .Init_pcb = s_FMKNVM_TemplateInit,
    .GetGeometry_pcb = s_FMKNVM_TemplateGetGeometry,
    .GetCapabilities_pcb = s_FMKNVM_TemplateGetCapabilities,
    .Read_pcb = s_FMKNVM_TemplateRead,
    .Program_pcb = s_FMKNVM_TemplateProgram,
    .Erase_pcb = s_FMKNVM_TemplateErase,
    .IsBusy_pcb = s_FMKNVM_TemplateIsBusy,
    .GetOperationResult_pcb = s_FMKNVM_TemplateGetOperationResult,
    .Cyclic_pcb = s_FMKNVM_TemplateCyclic
};

/*********************************
 * FMKNVM_BackendTemplate_GetApi
 *********************************/
const t_sFMKNVM_BackendApi * FMKNVM_BackendTemplate_GetApi(void)
{
    const t_sFMKNVM_BackendApi * BackendApi_ps = &c_FMKNVM_BackendTemplateApi_s;

    //---- 1- Return the lifetime-stable callback contract ----//

    return BackendApi_ps;
}

/*********************************
 * s_FMKNVM_TemplateInit
 *********************************/
static t_eReturnCode s_FMKNVM_TemplateInit(void * f_Context_pv)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate and initialize the backend context ----//
    if(f_Context_pv == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else
    {
        t_sFMKNVM_BackendTemplateContext * Context_ps = (t_sFMKNVM_BackendTemplateContext *)f_Context_pv;

        Context_ps->lastOperationResult_e = RC_OK;
        Context_ps->isBusy_b = FALSE;
        Context_ps->isInitialized_b = TRUE;

        /* CAUTION : Automatic generated code section for Init: Start */

        /* CAUTION : Automatic generated code section for Init: End */
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_TemplateGetGeometry
 *********************************/
static t_eReturnCode s_FMKNVM_TemplateGetGeometry(void * f_Context_pv, t_sFMKNVM_BackendGeometry * f_Geometry_ps)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate pointers and initialization ----//
    if(f_Context_pv == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_Geometry_ps == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else
    {
        t_sFMKNVM_BackendTemplateContext * Context_ps = (t_sFMKNVM_BackendTemplateContext *)f_Context_pv;

        if(Context_ps->isInitialized_b == FALSE)
        {
            Ret_e = RC_ERROR_INSTANCE_NOT_INITIALIZED;
        }
        else
        {
            //---- 2- Export the generated physical geometry ----//
            f_Geometry_ps->storageSize_u32 = FMKNVM_TEMPLATE_STORAGE_SIZE;
            f_Geometry_ps->eraseUnitSize_u32 = FMKNVM_TEMPLATE_ERASE_UNIT_SIZE;
            f_Geometry_ps->programUnitSize_u32 = FMKNVM_TEMPLATE_PROGRAM_UNIT_SIZE;
            f_Geometry_ps->commitUnitSize_u32 = FMKNVM_TEMPLATE_COMMIT_UNIT_SIZE;
        }
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_TemplateGetCapabilities
 *********************************/
static t_eReturnCode s_FMKNVM_TemplateGetCapabilities(   void * f_Context_pv,
                                                         t_sFMKNVM_BackendCapabilities * f_Capabilities_ps)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate pointers and initialization ----//
    if(f_Context_pv == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_Capabilities_ps == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else
    {
        t_sFMKNVM_BackendTemplateContext * Context_ps = (t_sFMKNVM_BackendTemplateContext *)f_Context_pv;

        if(Context_ps->isInitialized_b == FALSE)
        {
            Ret_e = RC_ERROR_INSTANCE_NOT_INITIALIZED;
        }
        else
        {
            //---- 2- Export the generated operational capabilities ----//
            f_Capabilities_ps->eraseRequired_b = FALSE;
            f_Capabilities_ps->readWhileWriteSupported_b = FALSE;
            f_Capabilities_ps->maxTransferSize_u32 = FMKNVM_TEMPLATE_STORAGE_SIZE;
        }
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_TemplateRead
 *********************************/
static t_eReturnCode s_FMKNVM_TemplateRead(   void * f_Context_pv,
                                              t_uint32 f_Address_u32,
                                              t_uint8 * f_Data_pu8,
                                              t_uint32 f_Size_u32)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the synchronous read request ----//
    if(f_Context_pv == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_Data_pu8 == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if((f_Size_u32 == 0U) || (f_Address_u32 >= FMKNVM_TEMPLATE_STORAGE_SIZE) || (f_Size_u32 >
             (FMKNVM_TEMPLATE_STORAGE_SIZE - f_Address_u32)))
    {
        Ret_e = RC_ERROR_PARAM_INVALID;
    }
    else
    {
        //---- 2- Execute or start the generated physical read ----//
        Ret_e = RC_ERROR_NOT_SUPPORTED;

        /* CAUTION : Automatic generated code section for Read: Start */

        /* CAUTION : Automatic generated code section for Read: End */
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_TemplateProgram
 *********************************/
static t_eReturnCode s_FMKNVM_TemplateProgram(   void * f_Context_pv,
                                                 t_uint32 f_Address_u32,
                                                 const t_uint8 * f_Data_pcu8,
                                                 t_uint32 f_Size_u32)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the synchronous program request ----//
    if(f_Context_pv == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_Data_pcu8 == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if((f_Size_u32 == 0U) || (f_Address_u32 >= FMKNVM_TEMPLATE_STORAGE_SIZE) || (f_Size_u32 >
             (FMKNVM_TEMPLATE_STORAGE_SIZE - f_Address_u32)))
    {
        Ret_e = RC_ERROR_PARAM_INVALID;
    }
    else
    {
        //---- 2- Execute or start the generated physical program ----//
        Ret_e = RC_ERROR_NOT_SUPPORTED;

        /* CAUTION : Automatic generated code section for Program: Start */

        /* CAUTION : Automatic generated code section for Program: End */
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_TemplateErase
 *********************************/
static t_eReturnCode s_FMKNVM_TemplateErase(void * f_Context_pv, t_uint32 f_Address_u32, t_uint32 f_Size_u32)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate the synchronous erase request ----//
    if(f_Context_pv == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if((f_Size_u32 == 0U) || (f_Address_u32 >= FMKNVM_TEMPLATE_STORAGE_SIZE) || (f_Size_u32 >
             (FMKNVM_TEMPLATE_STORAGE_SIZE - f_Address_u32)))
    {
        Ret_e = RC_ERROR_PARAM_INVALID;
    }
    else
    {
        //---- 2- Execute or start the generated physical erase ----//
        Ret_e = RC_ERROR_NOT_SUPPORTED;

        /* CAUTION : Automatic generated code section for Erase: Start */

        /* CAUTION : Automatic generated code section for Erase: End */
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_TemplateIsBusy
 *********************************/
static t_bool s_FMKNVM_TemplateIsBusy(void * f_Context_pv)
{
    t_bool isBusy_b = FALSE;

    //---- 1- Return the current asynchronous backend state ----//
    if(f_Context_pv != NULL)
    {
        t_sFMKNVM_BackendTemplateContext * Context_ps = (t_sFMKNVM_BackendTemplateContext *)f_Context_pv;

        isBusy_b = Context_ps->isBusy_b;
    }

    return isBusy_b;
}

/*********************************
 * s_FMKNVM_TemplateGetOperationResult
 *********************************/
static t_eReturnCode s_FMKNVM_TemplateGetOperationResult(void * f_Context_pv, t_eReturnCode * f_OperationResult_pe)
{
    t_eReturnCode Ret_e = RC_OK;

    //---- 1- Validate pointers and initialization ----//
    if(f_Context_pv == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else if(f_OperationResult_pe == NULL)
    {
        Ret_e = RC_ERROR_PTR_NULL;
    }
    else
    {
        t_sFMKNVM_BackendTemplateContext * Context_ps = (t_sFMKNVM_BackendTemplateContext *)f_Context_pv;

        if(Context_ps->isInitialized_b == FALSE)
        {
            Ret_e = RC_ERROR_INSTANCE_NOT_INITIALIZED;
        }
        else
        {
            //---- 2- Publish the latest completed physical result ----//
            *f_OperationResult_pe = Context_ps->lastOperationResult_e;
        }
    }

    return Ret_e;
}

/*********************************
 * s_FMKNVM_TemplateCyclic
 *********************************/
static void s_FMKNVM_TemplateCyclic(void * f_Context_pv)
{
    //---- 1- Progress one bounded generated backend step ----//
    (void)f_Context_pv;

    /* CAUTION : Automatic generated code section for Cyclic: Start */

    /* CAUTION : Automatic generated code section for Cyclic: End */
}
