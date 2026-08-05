/*********************************************************************
 * @file        FMKNVM_ConfigPublic.h
 * @brief       Public generated configuration for FMK_NVM.
 * @note        Copy or generate this template into FMKCFG_ConfigFiles.
 *********************************************************************/
#ifndef FMKNVM_CONFIGPUBLIC_H_INCLUDED
#define FMKNVM_CONFIGPUBLIC_H_INCLUDED

#include "TypeCommon.h"

/// @brief Identifies every transactional persistent partition.
typedef enum __t_eFMKNVM_PartitionId
{
    /* CAUTION : Automatic generated code section for Partition Enum: Start */

    /* CAUTION : Automatic generated code section for Partition Enum: End */

    FMKNVM_PARTITION_NB  ///< Number of configured persistent partitions.
} t_eFMKNVM_PartitionId;

/// @brief Identifies every fixed-size logical object stored by FMK_NVM.
typedef enum __t_eFMKNVM_ObjectId
{
    /* CAUTION : Automatic generated code section for Object Enum: Start */

    /* CAUTION : Automatic generated code section for Object Enum: End */

    FMKNVM_OBJECT_NB  ///< Number of configured logical objects.
} t_eFMKNVM_ObjectId;

#endif // FMKNVM_CONFIGPUBLIC_H_INCLUDED
