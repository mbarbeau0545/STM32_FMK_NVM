/*********************************************************************
 * @file        FMKNVM_ConfigPrivate.h
 * @brief       Private generated configuration for FMK_NVM.
 * @note        Copy or generate this template into FMKCFG_ConfigFiles.
 *********************************************************************/
#ifndef FMKNVM_CONFIGPRIVATE_H_INCLUDED
#define FMKNVM_CONFIGPRIVATE_H_INCLUDED

#include "FMK_HAL/FMK_NVM/Src/FMK_NVM.h"

/// @brief Storage technology selected for the generated project.
#define FMKNVM_EEPROM_TYPE FMKNVM_EEPROM_TYPE_FLASH_H753

/// @brief Maximum number of pending automatic partition commits.
#define FMKNVM_REQUEST_FIFO_SIZE ((t_uint16)8U)

/// @brief Maximum payload capacity of one configured partition.
#define FMKNVM_MAX_PARTITION_PAYLOAD_CAPACITY ((t_uint32)512U)

/// @brief Bytes reserved for logical-object validity flags.
#define FMKNVM_OBJECT_VALIDITY_BITMAP_SIZE \
    ((t_uint32)(((t_uint32)FMKNVM_OBJECT_NB + 7U) / 8U))

/// @brief Maximum number of retries for one persistent transaction.
#define FMKNVM_MAX_WRITE_RETRY ((t_uint8)3U)

/// @brief Magic value stored in every persistent record header.
#define FMKNVM_RECORD_MAGIC ((t_uint32)0x4E564D31UL)

/// @brief Marker programmed after successful record verification.
#define FMKNVM_COMMIT_MARKER ((t_uint32)0x43A65C9DUL)

/// @brief Lists the storage technologies selectable for FMK_NVM.
typedef enum __t_eFMKNVM_EepromType
{
    FMKNVM_EEPROM_TYPE_FLASH_H753 = 0U,  ///< STM32H753 internal Flash.
    FMKNVM_EEPROM_TYPE_FLASH_G4,         ///< STM32G4 internal Flash.
    FMKNVM_EEPROM_TYPE_I2C,              ///< External I2C EEPROM.
    FMKNVM_EEPROM_TYPE_SPI,              ///< External SPI EEPROM.
    FMKNVM_EEPROM_TYPE_NB                ///< Number of selectable backends.
} t_eFMKNVM_EepromType;

/// @brief Defines one partition cache and its automatic commit policy.
typedef struct __t_sFMKNVM_PartitionDescriptor
{
    t_uint8 * cacheData_pu8;              ///< Module-owned partition cache.
    t_uint32 payloadCapacity_u32;          ///< Fixed payload size in bytes.
    t_uint16 formatVersion_u16;            ///< Serialized partition version.
    t_uint32 quietPeriodMs_u32;            ///< Delay after the latest update.
    t_uint32 maximumDeferredCommitMs_u32;  ///< Maximum dirty duration.
    t_uint32 minimumCommitIntervalMs_u32;  ///< Minimum period between commits.
} t_sFMKNVM_PartitionDescriptor;

/// @brief Defines one fixed-size logical object inside a partition cache.
typedef struct __t_sFMKNVM_ObjectDescriptor
{
    t_eFMKNVM_PartitionId partitionId_e;  ///< Owning persistent partition.
    t_uint32 offset_u32;                  ///< Byte offset inside the cache.
    t_uint32 size_u32;                    ///< Exact logical object size.
    t_uint16 version_u16;                 ///< Logical object format version.
} t_sFMKNVM_ObjectDescriptor;

/* CAUTION : Automatic generated code section for Partition Cache: Start */

/* CAUTION : Automatic generated code section for Partition Cache: End */

/// @brief Generated logical partition definitions and commit policies.
static const t_sFMKNVM_PartitionDescriptor
    c_FMKNVM_PartitionDescriptors_as[FMKNVM_PARTITION_NB] =
{
    /* CAUTION : Automatic generated code section for Partition Cfg: Start */

    /* CAUTION : Automatic generated code section for Partition Cfg: End */
};

/// @brief Generated mapping of logical objects into partition caches.
static const t_sFMKNVM_ObjectDescriptor
    c_FMKNVM_ObjectDescriptors_as[FMKNVM_OBJECT_NB] =
{
    /* CAUTION : Automatic generated code section for Object Cfg: Start */

    /* CAUTION : Automatic generated code section for Object Cfg: End */
};

_Static_assert(FMKNVM_EEPROM_TYPE < FMKNVM_EEPROM_TYPE_NB,
               "FMK_NVM EEPROM type is invalid");

#endif // FMKNVM_CONFIGPRIVATE_H_INCLUDED
