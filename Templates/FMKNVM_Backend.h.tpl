/*********************************************************************
 * @file        FMKNVM_BackendTemplate.h
 * @brief       Generated concrete backend contract for FMK_NVM.
 * @note        Replace BackendTemplate with the concrete technology name.
 *********************************************************************/
#ifndef FMKNVM_BACKENDTEMPLATE_H_INCLUDED
#define FMKNVM_BACKENDTEMPLATE_H_INCLUDED

#include "FMK_HAL/FMK_NVM/Src/FMKNVM_Backend.h"

/**
 * @brief Return the immutable API of the generated concrete NVM backend.
 * @note The backend owns its context for the complete program lifetime. The
 *       returned callback table and its context must never be temporary.
 *
 * @retval Valid pointer Immutable generated backend API.
 * @retval NULL The generated backend is unavailable on the selected target.
 */
const t_sFMKNVM_BackendApi * FMKNVM_BackendTemplate_GetApi(void);

#endif // FMKNVM_BACKENDTEMPLATE_H_INCLUDED
