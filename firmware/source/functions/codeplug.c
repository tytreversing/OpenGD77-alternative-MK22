/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2024 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 *
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include "functions/codeplug.h"
#include "hardware/EEPROM.h"
#include "hardware/SPI_Flash.h"
#include "functions/trx.h"
#include "usb/usb_com.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiGlobals.h"
#include "interfaces/settingsStorage.h"


const int CODEPLUG_ADDR_EX_ZONE_BASIC = 0x8000;
const int CODEPLUG_ADDR_EX_ZONE_INUSE_PACKED_DATA = 0x8010;
#define CODEPLUG_EX_ZONE_INUSE_PACKED_DATA_SIZE  32
const int CODEPLUG_ADDR_EX_ZONE_LIST = 0x8030;

const int CODEPLUG_ZONE_MAX_COUNT = 250;
const int CODEPLUG_ADDR_CHANNEL_EEPROM = 0x3790;
const int CODEPLUG_ADDR_CHANNEL_HEADER_EEPROM = 0x3780; // CODEPLUG_ADDR_CHANNEL_EEPROM - 16
const int CODEPLUG_ADDR_CHANNEL_FLASH = 0x7B1C0;
const int CODEPLUG_ADDR_CHANNEL_HEADER_FLASH = 0x7B1B0; // CODEPLUG_ADDR_CHANNEL_FLASH - 16

const int CODEPLUG_ADDR_SIGNALLING_DTMF = 0x1400;
const int CODEPLUG_ADDR_SIGNALLING_DTMF_DURATIONS = (CODEPLUG_ADDR_SIGNALLING_DTMF + 0x72); // offset to grab the DTMF durations
const int CODEPLUG_SIGNALLING_DTMF_DURATIONS_SIZE = 4;

const int CODEPLUG_ADDR_RX_GROUP_LEN = 0x8D620;  // 76 TG lists
const int CODEPLUG_ADDR_RX_GROUP = 0x8D6A0;//

const int CODEPLUG_ADDR_CONTACTS = 0x87620;

const int CODEPLUG_ADDR_DTMF_CONTACTS = 0x02f88;

const int CODEPLUG_ADDR_USER_DMRID = 0x00E8;
const int CODEPLUG_ADDR_USER_CALLSIGN = 0x00E0;

const int CODEPLUG_ADDR_GENERAL_SETTINGS = 0x00E0;

const int CODEPLUG_ADDR_BOOT_INTRO_SCREEN = 0x7518;// 0x01 = Chars 0x00 = Picture
const int CODEPLUG_ADDR_BOOT_PASSWORD_ENABLE= 0x7519;// 0x00 = password disabled 0x01 = password enable
const int CODEPLUG_ADDR_BOOT_PASSWORD_AREA = 0x751C;// Seems to be 3 bytes coded as BCD e.f. 0x12 0x34 0x56
const int CODEPLUG_BOOT_PASSWORD_LEN = 3;
const int CODEPLUG_ADDR_BOOT_LINE1 = 0x7540;
const int CODEPLUG_ADDR_BOOT_LINE2 = 0x7550;
const int CODEPLUG_ADDR_VFO_A_CHANNEL = 0x7590;
const int CPS_CODEPLUG_SECTION_2_START = 0x7500;

const int CODEPLUG_ADDR_APRS_CONFIGS = 0x1588;// Address of start of APRS configs (formerly Emergency Systems)

int codeplugChannelsPerZone = 16;

const int VFO_FREQ_STEP_TABLE[8] = {250,500,625,1000,1250,2500,3000,5000};
const int VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE[7] = {78,157,313,625, 1250,2500,5000};


const int CODEPLUG_MAX_VARIABLE_SQUELCH = 21U;
const int CODEPLUG_MIN_VARIABLE_SQUELCH = 1U;

const int CODEPLUG_MIN_PER_CHANNEL_POWER  = 1;

const int CODEPLUG_ADDR_DEVICE_INFO = 0x80;
const int CODEPLUG_ADDR_DEVICE_INFO_READ_SIZE = 96;// (sizeof struct_codeplugDeviceInfo_t)

const int CODEPLUG_ADDR_BOOT_PASSWORD_PIN = 0x7518;

const uint16_t APRS_MAGIC_VERSION = 0x4152;


static uint16_t allChannelsTotalNumOfChannels = 0;
static uint16_t allChannelsHighestChannelIndex = 0;

typedef struct
{
	uint32_t tgOrPCNum;
	uint16_t index;
} codeplugContactCache_t;


typedef struct
{
	uint8_t index;
} codeplugDTMFContactCache_t;

typedef struct
{
	int numTGContacts;
	int numPCContacts;
	int numALLContacts;
	int numDTMFContacts;
	codeplugContactCache_t contactsLookupCache[CODEPLUG_CONTACTS_MAX];
	codeplugDTMFContactCache_t contactsDTMFLookupCache[CODEPLUG_DTMF_CONTACTS_MAX];
} codeplugContactsCache_t;

typedef struct
{
	int 	numOfConfigs;
} codeplugAPRSConfigsCache_t;

typedef struct
{
	int dataType;
	int dataLength;
} codeplugCustomDataBlockHeader_t;

__attribute__((section(".data.$RAM2"))) codeplugContactsCache_t codeplugContactsCache;

__attribute__((section(".data.$RAM2"))) uint8_t codeplugRXGroupCache[CODEPLUG_RX_GROUPLIST_MAX];
__attribute__((section(".data.$RAM2"))) uint8_t codeplugAllChannelsCache[128];
__attribute__((section(".data.$RAM2"))) uint8_t codeplugZonesInUseCache[CODEPLUG_EX_ZONE_INUSE_PACKED_DATA_SIZE];
__attribute__((section(".data.$RAM2"))) uint16_t quickKeysCache[CODEPLUG_QUICKKEYS_SIZE];

__attribute__((section(".data.$RAM2"))) uint8_t lastUsedChannelInZoneData[CODEPLUG_ALL_ZONES_MAX + 1]; // All zones (0..79) + AllChannel 0..1023 (hence one extra byte to store this value)
static bool lastUsedChannelInZoneHasChanged = false;

__attribute__((section(".data.$RAM2"))) codeplugAPRSConfigsCache_t codeplugAPRSCache;

static bool codeplugContactGetReserve1ByteForIndex(int index, struct_codeplugContact_t *contact);

uint32_t byteSwap32(uint32_t n)
{
    return ((((n) & 0x000000FFU) << 24U) | (((n) & 0x0000FF00U) << 8U) | (((n) & 0x00FF0000U) >> 8U) | (((n) & 0xFF000000U) >> 24U));// from usb_misc.h
}

uint32_t byteSwap16(uint16_t n)
{
    return  (((n) & 0x00FFU) << 8U) | (((n) & 0xFF00U) >> 8U);
}

// BCD encoding to integer conversion
uint32_t bcd2int(uint32_t i)
{
    uint32_t result = 0;
    int multiplier = 1;
    while (i)
    {
        result += (i & 0x0f) * multiplier;
        multiplier *= 10;
        i = i >> 4;
    }
    return result;
}

// Needed to convert the legacy DMR ID data which uses BCD encoding for the DMR ID numbers
uint32_t int2bcd(uint32_t i)
{
	uint32_t result = 0;
    int shift = 0;

    while (i)
    {
        result +=  (i % 10) << shift;
        i = i / 10;
        shift += 4;
    }
    return result;
}

uint16_t bcd2uint16(uint16_t i)
{
    int result = 0;
    int multiplier = 1;
    while (i)
    {
        result += (i & 0x0f) * multiplier;
        multiplier *= 10;
        i = i >> 4;
    }
    return result;
}

CodeplugCSSTypes_t codeplugGetCSSType(uint16_t tone)
{
	if ((tone == CODEPLUG_CSS_TONE_NONE) || (tone == 0x0))
	{
		return CSS_TYPE_NONE;
	}
	else if((tone != CODEPLUG_CSS_TONE_NONE) && (tone != 0x0))
	{
		if ((tone & CSS_TYPE_DCS_MASK) == 0)
		{
			return CSS_TYPE_CTCSS;
		}
		else if (tone & CSS_TYPE_DCS_INVERTED)
		{
			return (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED);
		}

		return CSS_TYPE_DCS;
	}

	return CSS_TYPE_NONE;
}

// Converts a codeplug coded-squelch system value to int (with flags if DCS)
uint16_t codeplugCSSToInt(uint16_t tone)
{
	CodeplugCSSTypes_t type = codeplugGetCSSType(tone);

	// Only the CTCSS needs convertion
	if (type == CSS_TYPE_CTCSS)
	{
		return bcd2int(tone);
	}

	return ((tone == 0x0) ? CODEPLUG_CSS_TONE_NONE : tone);
}

// Converts an int (with flags if DCS) to codeplug coded-squelch system value
uint16_t codeplugIntToCSS(uint16_t tone)
{
	CodeplugCSSTypes_t type = codeplugGetCSSType(tone);

	// Only the CTCSS needs convertion
	if (type == CSS_TYPE_CTCSS)
	{
		return int2bcd(tone);
	}

	return ((tone == 0x0) ? CODEPLUG_CSS_TONE_NONE : tone);
}

void codeplugUtilConvertBufToString(char *codeplugBuf, char *outBuf, int len)
{
	for(int i = 0; i < len; i++)
	{
		if (codeplugBuf[i] == 0xff)
		{
			codeplugBuf[i] = 0;
		}
		outBuf[i] = codeplugBuf[i];
	}
	outBuf[len] = 0;
}

void codeplugUtilConvertStringToBuf(char *inBuf, char *outBuf, int len)
{
	memset(outBuf,0xff,len);
	for (int i = 0; i < len; i++)
	{
		if (inBuf[i] == 0x00)
		{
			break;
		}
		outBuf[i] = inBuf[i];
	}
}

void codeplugZonesInitCache(void)
{
	EEPROM_Read(CODEPLUG_ADDR_EX_ZONE_INUSE_PACKED_DATA, (uint8_t *)&codeplugZonesInUseCache, CODEPLUG_EX_ZONE_INUSE_PACKED_DATA_SIZE);
}

int codeplugZonesGetCount(void)
{
	int numZones = 1;// Add one extra zone to allow for the special 'All Channels' Zone

	for(int i = 0; i < CODEPLUG_EX_ZONE_INUSE_PACKED_DATA_SIZE; i++)
	{
		numZones += __builtin_popcount(codeplugZonesInUseCache[i]);
	}

	return numZones;
}

bool codeplugZoneGetDataForNumber(int zoneNum, struct_codeplugZone_t *returnBuf)
{
	if (zoneNum == (codeplugZonesGetCount() - 1)) //special case: return a special Zone called 'All Channels'
	{
		int nameLen = SAFE_MIN(((int)sizeof(returnBuf->name)), ((int)strlen(currentLanguage->all_channels)));

		// Codeplug name is 0xff filled, codeplugUtilConvertBufToString() handles the conversion
		memset(returnBuf->name, 0xff, sizeof(returnBuf->name));
		memcpy(returnBuf->name, currentLanguage->all_channels, nameLen);

		// set all channels to zero, All Channels is handled separately
		memset(returnBuf->channels, 0, codeplugChannelsPerZone);

		returnBuf->NOT_IN_CODEPLUGDATA_numChannelsInZone = allChannelsTotalNumOfChannels;
		returnBuf->NOT_IN_CODEPLUGDATA_highestIndex = allChannelsHighestChannelIndex;
		returnBuf->NOT_IN_CODEPLUGDATA_indexNumber = -1;// Set as -1 as this is not a real zone. Its the "All Channels" zone
		return true;
	}
	else
	{
		// Need to find the index into the Zones data for the specific Zone number.
		// Because the Zones data is not guaranteed to be packed by the CPS (though we should attempt to make the CPS always pack the Zones)
		int count = -1;// Need to start counting at -1 because the Zone number is zero indexed
		int foundIndex = -1;

		// Go though each byte in the In Use table
		for(int i = 0; i < CODEPLUG_EX_ZONE_INUSE_PACKED_DATA_SIZE; i++)
		{
			// Go though each binary bit, counting them one by one
			for(int j = 0; j < 8; j++)
			{
				if (((codeplugZonesInUseCache[i] >> j) & 0x01) == 0x01)
				{
					count++;

					if (count == zoneNum)
					{
						// found it. So save the index before we exit the "for" loops
						foundIndex = (i * 8) + j;
						break;// Will break out of this loop, but the outer loop breaks because it also checks for foundIndex
					}
				}
			}
		}

		if (foundIndex != -1)
		{
			// Save this in case we need to add channels to a zone and hence need the index number so it can be saved back to the codeplug memory
			returnBuf->NOT_IN_CODEPLUGDATA_indexNumber = foundIndex;

			// IMPORTANT. Write size is different from the size of the data, because it the zone struct contains properties not in the codeplug data
			EEPROM_Read(CODEPLUG_ADDR_EX_ZONE_LIST + (foundIndex * (16 + (sizeof(uint16_t) * codeplugChannelsPerZone))),
					(uint8_t *)returnBuf, ((codeplugChannelsPerZone == 16) ? CODEPLUG_ZONE_DATA_ORIGINAL_STRUCT_SIZE : CODEPLUG_ZONE_DATA_OPENGD77_STRUCT_SIZE));


			for(int i = 0; i < codeplugChannelsPerZone; i++)
			{
				// Empty channels seem to be filled with zeros, and zone could be full of channels.
				if ((returnBuf->channels[i] == 0) || (i == (codeplugChannelsPerZone - 1)))
				{
					returnBuf->NOT_IN_CODEPLUGDATA_highestIndex = returnBuf->NOT_IN_CODEPLUGDATA_numChannelsInZone = (i + ((returnBuf->channels[i] == 0) ? 0 : 1));
					return true;
				}
			}
		}

		memset(returnBuf->channels, 0, codeplugChannelsPerZone);
		returnBuf->NOT_IN_CODEPLUGDATA_highestIndex = returnBuf->NOT_IN_CODEPLUGDATA_numChannelsInZone = 0;
		returnBuf->NOT_IN_CODEPLUGDATA_indexNumber = -2; // we could not use '-1' on error, as -1 is All Channel zone
	}

	return false;
}

bool codeplugZoneAddChannelToZoneAndSave(int channelIndex, struct_codeplugZone_t *zoneBuf)
{
	if ((zoneBuf->NOT_IN_CODEPLUGDATA_numChannelsInZone < codeplugChannelsPerZone) && (zoneBuf->NOT_IN_CODEPLUGDATA_indexNumber != -1))
	{
		zoneBuf->channels[zoneBuf->NOT_IN_CODEPLUGDATA_numChannelsInZone++] = channelIndex;// add channel to zone, and increment numb channels in zone
		zoneBuf->NOT_IN_CODEPLUGDATA_highestIndex = zoneBuf->NOT_IN_CODEPLUGDATA_numChannelsInZone;

		// IMPORTANT. Write size is different from the size of the data, because it the zone struct contains properties not in the codeplug data
		return EEPROM_Write(CODEPLUG_ADDR_EX_ZONE_LIST + (zoneBuf->NOT_IN_CODEPLUGDATA_indexNumber * (16 + (sizeof(uint16_t) * codeplugChannelsPerZone))),
				(uint8_t *)zoneBuf, ((codeplugChannelsPerZone == 16) ? CODEPLUG_ZONE_DATA_ORIGINAL_STRUCT_SIZE : CODEPLUG_ZONE_DATA_OPENGD77_STRUCT_SIZE));
	}

	return false;
}

static uint16_t codeplugAllChannelsGetCount(void)
{
	uint16_t c = 0;

	//                         v-- This is a bit verbose, but it does make it clear
	for (uint16_t i = 0; i < ((CODEPLUG_CHANNELS_BANKS_MAX * CODEPLUG_CHANNELS_PER_BANK) / 8); i++)
	{
		c += __builtin_popcount(codeplugAllChannelsCache[i]);
	}

	return c;
}

static bool codeplugAllChannelsReadHeaderBank(int channelBank, uint8_t *bitArray)
{
	if(channelBank == 0)
	{
		return EEPROM_Read(CODEPLUG_ADDR_CHANNEL_HEADER_EEPROM, bitArray, 16);
	}

	return SPI_Flash_read(FLASH_ADDRESS_OFFSET + CODEPLUG_ADDR_CHANNEL_HEADER_FLASH + ((channelBank - 1) *
			(CODEPLUG_CHANNELS_PER_BANK * CODEPLUG_CHANNEL_DATA_STRUCT_SIZE + 16)), bitArray, 16);
}

bool codeplugAllChannelsIndexIsInUse(int index)
{
	if ((index >= CODEPLUG_CHANNELS_MIN) && (index <= CODEPLUG_CHANNELS_MAX))
	{
		index--;
		return (((codeplugAllChannelsCache[index / 8] >> (index % 8)) & 0x01) != 0);
	}

	return false;
}

void codeplugAllChannelsIndexSetUsed(int index)
{
	if ((index >= CODEPLUG_CHANNELS_MIN) && (index <= CODEPLUG_CHANNELS_MAX))
	{
		index--;
		int channelBank = (index / CODEPLUG_CHANNELS_PER_BANK);
		int byteno = (index % CODEPLUG_CHANNELS_PER_BANK) / 8;
		int cacheOffset = index / 8;

		codeplugAllChannelsCache[cacheOffset] |= (1 << (index % 8));

		if(channelBank == 0)
		{
			EEPROM_Write(CODEPLUG_ADDR_CHANNEL_HEADER_EEPROM + byteno, &codeplugAllChannelsCache[cacheOffset], 1);
		}
		else
		{
			SPI_Flash_write(FLASH_ADDRESS_OFFSET + (CODEPLUG_ADDR_CHANNEL_HEADER_FLASH + ((channelBank - 1) *
					(CODEPLUG_CHANNELS_PER_BANK * CODEPLUG_CHANNEL_DATA_STRUCT_SIZE + 16))) + byteno, &codeplugAllChannelsCache[cacheOffset], 1);
		}

		allChannelsTotalNumOfChannels++;
		if ((index + 1) > allChannelsHighestChannelIndex)
		{
			allChannelsHighestChannelIndex = (index + 1);
		}
	}
}

void codeplugAllChannelsInitCache(void)
{
	// There are 8 banks
	for (uint16_t bank = 0; bank < CODEPLUG_CHANNELS_BANKS_MAX; bank++)
	{
		// Of 128 channels
		codeplugAllChannelsReadHeaderBank(bank, &codeplugAllChannelsCache[bank * 16]);
	}

	allChannelsHighestChannelIndex = 0;
	for (uint16_t index = CODEPLUG_CHANNELS_MAX; index >= CODEPLUG_CHANNELS_MIN ; index--)
	{
		if (codeplugAllChannelsIndexIsInUse(index))
		{
			allChannelsHighestChannelIndex = index;
			break;
		}
	}

	allChannelsTotalNumOfChannels = codeplugAllChannelsGetCount();
}

uint32_t codeplugChannelGetOptionalDMRID(struct_codeplugChannel_t *channelBuf)
{
	uint32_t retVal = 0;

	if (codeplugChannelGetFlag(channelBuf, CHANNEL_FLAG_OPTIONAL_DMRID) != 0)
	{
		retVal = ((channelBuf->rxSignaling << 16) | (channelBuf->artsInterval << 8) | channelBuf->encrypt);
	}

	return retVal;
}

void codeplugChannelSetOptionalDMRID(struct_codeplugChannel_t *channelBuf, uint32_t dmrID)
{
	uint32_t tmpID = 0x00001600;
	bool dmrIDIsValid = ((dmrID >= MIN_TG_OR_PC_VALUE) && (dmrID <= MAX_TG_OR_PC_VALUE));

	//  Default values are:
	//
	//	rxSignaling = 0x00  e.g. SignalingSystemE.Off
	//  artsInterval = 22 = 0x16
	//  encrypt = 0x00;
	//

	// Set DMRId and flag, if valid.
	if (dmrIDIsValid)
	{
		tmpID = dmrID;
	}

	codeplugChannelSetFlag(channelBuf, CHANNEL_FLAG_OPTIONAL_DMRID, (dmrIDIsValid ? 1 : 0));

	channelBuf->rxSignaling = (tmpID >> 16) & 0xFF;
	channelBuf->artsInterval = (tmpID >> 8) & 0xFF;
	channelBuf->encrypt = tmpID & 0xFF;
}

// -----------------------------------
// -- Codeplug channel flag helpers --
// -----------------------------------
typedef uint8_t *(*flagGetter_t)(struct_codeplugChannel_t *chan);
static uint8_t *_getLDMRFlag1(struct_codeplugChannel_t *chan)
{
	return &chan->LibreDMR_flag1;
}
static uint8_t *_getFlag2(struct_codeplugChannel_t *chan)
{
	return &chan->flag2;
}
static uint8_t *_getFlag3(struct_codeplugChannel_t *chan)
{
	return &chan->flag3;
}
static uint8_t *_getFlag4(struct_codeplugChannel_t *chan)
{
	return &chan->flag4;
}

struct
{
		flagGetter_t getter;
		uint8_t      mask;
		uint8_t      shift;
} flagAccessor[] = // The order of this array HAS to follow ChannelFlag_t enum
{
		// LibreDMR_flag1
		{ _getLDMRFlag1, CODEPLUG_CHANNEL_LIBREDMR_FLAG1_OPTIONAL_DMRID, 7 }, // CHANNEL_FLAG_OPTIONAL_DMRID
		{ _getLDMRFlag1, CODEPLUG_CHANNEL_LIBREDMR_FLAG1_NO_BEEP,        6 }, // CHANNEL_FLAG_NO_BEEP,
		{ _getLDMRFlag1, CODEPLUG_CHANNEL_LIBREDMR_FLAG1_NO_ECO,         5 }, // CHANNEL_FLAG_NO_ECO,
		{ _getLDMRFlag1, CODEPLUG_CHANNEL_LIBREDMR_FLAG1_OUT_OF_BAND,    4 }, // CHANNEL_FLAG_OUT_OF_BAND,
		{ _getLDMRFlag1, CODEPLUG_CHANNEL_LIBREDMR_FLAG1_USE_LOCATION,   3 }, // CHANNEL_FLAG_USE_LOCATION
		{ _getLDMRFlag1, CODEPLUG_CHANNEL_LIBREDMR_FLAG1_FORCE_DMO,      2 }, // CHANNEL_FLAG_FORCE_DMO
		// flag2
		{ _getFlag2,     CODEPLUG_CHANNEL_FLAG2_TIMESLOT_TWO,            6 }, // CHANNEL_FLAG_TIMESLOT_TWO,
		// flag3
		{ _getFlag3,     CODEPLUG_CHANNEL_FLAG3_STE,                     6 }, // CHANNEL_FLAG_STE,
		{ _getFlag3,     CODEPLUG_CHANNEL_FLAG3_NON_STE,                 5 }, // CHANNEL_FLAG_NON_STE,
		// flag4
		{ _getFlag4,     CODEPLUG_CHANNEL_FLAG4_POWER,                   7 }, // CHANNEL_FLAG_POWER,
		{ _getFlag4,     CODEPLUG_CHANNEL_FLAG4_VOX,                     6 }, // CHANNEL_FLAG_VOX,
		{ _getFlag4,     CODEPLUG_CHANNEL_FLAG4_ZONE_SKIP,               5 }, // CHANNEL_FLAG_ZONE_SKIP,
		{ _getFlag4,     CODEPLUG_CHANNEL_FLAG4_ALL_SKIP,                4 }, // CHANNEL_FLAG_ALL_SKIP,
		// Talkaround is unused (bit #3)
		{ _getFlag4,     CODEPLUG_CHANNEL_FLAG4_RX_ONLY,                 2 }, // CHANNEL_FLAG_RX_ONLY,
		{ _getFlag4,     CODEPLUG_CHANNEL_FLAG4_BW_25K,                  1 }, // CHANNEL_FLAG_BW_25K,
		{ _getFlag4,     CODEPLUG_CHANNEL_FLAG4_SQUELCH,                 0 }  // CHANNEL_FLAG_SQUELCH,
};

//
// Returns 0, 1 or the value (e.g. CODEPLUG_CHANNEL_FLAG3_STE), right shifted
//
uint8_t codeplugChannelGetFlag(struct_codeplugChannel_t *channelBuf, ChannelFlag_t flag)
{
	return ((*flagAccessor[flag].getter(channelBuf) & flagAccessor[flag].mask) >> flagAccessor[flag].shift);
}

//
// Returns unshifted value
//
uint8_t codeplugChannelSetFlag(struct_codeplugChannel_t *channelBuf, ChannelFlag_t flag, uint8_t value)
{
	uint8_t *data = flagAccessor[flag].getter(channelBuf);

	*data &= ~flagAccessor[flag].mask;

	if (value)
	{
		*data |= (value << flagAccessor[flag].shift);
	}

	return value;
}
// ----------------------------------------
// -- END: Codeplug channel flag helpers --
// ----------------------------------------

void codeplugChannelGetDataWithOffsetAndLengthForIndex(int index, struct_codeplugChannel_t *channelBuf, uint8_t offset, int length)
{
	// lower 128 channels are in EEPROM. Remaining channels are in Flash ! (What a mess...)
	index--; // I think the channel index numbers start from 1 not zero.
	if (index < 128)
	{
		EEPROM_Read((CODEPLUG_ADDR_CHANNEL_EEPROM + index * CODEPLUG_CHANNEL_DATA_STRUCT_SIZE) + offset, ((uint8_t *)channelBuf) + offset, length);
	}
	else
	{
		int flashReadPos = CODEPLUG_ADDR_CHANNEL_FLASH;

		index -= 128;// First 128 channels are in the EEPROM, so subtract 128 from the number when looking in the Flash

		// Every 128 bytes there seem to be 16 bytes gaps. I don't know why,bits since 16*8 = 128 bits, its likely they are flag bytes to indicate which channel in the next block are in use
		flashReadPos += 16 * (index / 128);// we just need to skip over that these flag bits when calculating the position of the channel data in memory

		SPI_Flash_read(FLASH_ADDRESS_OFFSET + (flashReadPos + index * CODEPLUG_CHANNEL_DATA_STRUCT_SIZE) + offset, ((uint8_t *)channelBuf) + offset, length);
	}
}

void codeplugChannelGetDataForIndex(int index, struct_codeplugChannel_t *channelBuf)
{
	// Read the whole channel
	codeplugChannelGetDataWithOffsetAndLengthForIndex(index, channelBuf, 0, CODEPLUG_CHANNEL_DATA_STRUCT_SIZE);

	channelBuf->chMode = (channelBuf->chMode == 0) ? RADIO_MODE_ANALOG : RADIO_MODE_DIGITAL;
	// Convert legacy codeplug tx and rx freq values into normal integers
	channelBuf->txFreq = bcd2int(channelBuf->txFreq);
	channelBuf->rxFreq = bcd2int(channelBuf->rxFreq);
	channelBuf->txTone = codeplugCSSToInt(channelBuf->txTone);
	channelBuf->rxTone = codeplugCSSToInt(channelBuf->rxTone);

	channelBuf->NOT_IN_CODEPLUG_flag = 0x00;
	channelBuf->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 = -1;

	// Sanity check the sql value, because its not used by the official firmware and may contain random value e.g. 255
	if (channelBuf->sql > 21)
	{
		channelBuf->sql = 10;
	}
	/* 2020.10.27 vk3kyy - I don't think this is necessary as the function which loads the contact treats index = 0 as a special case and always loads TG 9
	 *
	// Sanity check the digital contact and set it to 1 is its not been assigned, even for FM channels, as the user could switch to DMR on this channel
	if (channelBuf->contact == 0)
	{
		channelBuf->contact = 1;
	}*/
}

bool codeplugChannelSaveDataForIndex(int index, struct_codeplugChannel_t *channelBuf)
{
	bool retVal = true;
#if defined(PLATFORM_MD9600)
	bool outOfBandFlag = ((channelBuf->LibreDMR_flag1 & CODEPLUG_CHANNEL_LIBREDMR_FLAG1_OUT_OF_BAND) != 0);

	// Clear the out of band flag, blindly.
	channelBuf->LibreDMR_flag1 &= ~CODEPLUG_CHANNEL_LIBREDMR_FLAG1_OUT_OF_BAND;
#endif

	channelBuf->chMode = (channelBuf->chMode == RADIO_MODE_ANALOG) ? 0 : 1;
	// Convert normal integers into legacy codeplug tx and rx freq values
	channelBuf->txFreq = int2bcd(channelBuf->txFreq);
	channelBuf->rxFreq = int2bcd(channelBuf->rxFreq);
	channelBuf->txTone = codeplugIntToCSS(channelBuf->txTone);
	channelBuf->rxTone = codeplugIntToCSS(channelBuf->rxTone);

	// lower 128 channels are in EEPROM. Remaining channels are in Flash ! (What a mess...)
	index--; // I think the channel index numbers start from 1 not zero.
	if (index < 128)
	{
		retVal = EEPROM_Write(CODEPLUG_ADDR_CHANNEL_EEPROM + index * CODEPLUG_CHANNEL_DATA_STRUCT_SIZE, (uint8_t *)channelBuf, CODEPLUG_CHANNEL_DATA_STRUCT_SIZE);
	}
	else
	{
		int flashWritePos = CODEPLUG_ADDR_CHANNEL_FLASH;
		int flashSector;
		int flashEndSector;
		int bytesToWriteInCurrentSector = CODEPLUG_CHANNEL_DATA_STRUCT_SIZE;

		index -= 128;// First 128 channels are in the EEPOM, so subtract 128 from the number when looking in the Flash

		// Every 128 bytes there seem to be 16 bytes gaps. I don't know why,bits since 16*8 = 128 bits, its likely they are flag bytes to indicate which channel in the next block are in use
		flashWritePos += 16 * (index / 128);// we just need to skip over that these flag bits when calculating the position of the channel data in memory
		flashWritePos += index * CODEPLUG_CHANNEL_DATA_STRUCT_SIZE;// go to the position of the specific index

		flashSector 	= flashWritePos / 4096;
		flashEndSector 	= (flashWritePos + CODEPLUG_CHANNEL_DATA_STRUCT_SIZE) / 4096;

		if (flashSector != flashEndSector)
		{
			bytesToWriteInCurrentSector = (flashEndSector * 4096) - flashWritePos;
		}

		SPI_Flash_read(FLASH_ADDRESS_OFFSET + (flashSector * 4096), SPI_Flash_sectorbuffer, 4096);
		uint8_t *writePos = SPI_Flash_sectorbuffer + flashWritePos - (flashSector * 4096);
		memcpy(writePos, channelBuf, bytesToWriteInCurrentSector);

		retVal = SPI_Flash_eraseSector(FLASH_ADDRESS_OFFSET + (flashSector * 4096));
		if (!retVal)
		{
			goto errorExit;
		}

		for (int i = 0; i < 16; i++)
		{
			retVal = SPI_Flash_writePage(FLASH_ADDRESS_OFFSET + (flashSector * 4096) + (i * 256), SPI_Flash_sectorbuffer + (i * 256));
			if (!retVal)
			{
				goto errorExit;
			}
		}

		if (flashSector != flashEndSector)
		{
			uint8_t *channelBufPusOffset = (uint8_t *)channelBuf + bytesToWriteInCurrentSector;
			bytesToWriteInCurrentSector = CODEPLUG_CHANNEL_DATA_STRUCT_SIZE - bytesToWriteInCurrentSector;

			SPI_Flash_read(FLASH_ADDRESS_OFFSET + (flashEndSector * 4096), SPI_Flash_sectorbuffer, 4096);
			memcpy(SPI_Flash_sectorbuffer, (uint8_t *)channelBufPusOffset, bytesToWriteInCurrentSector);

			retVal = SPI_Flash_eraseSector(FLASH_ADDRESS_OFFSET + (flashEndSector * 4096));

			if (!retVal)
			{
				goto errorExit;
			}
			for (int i = 0; i < 16; i++)
			{
				retVal = SPI_Flash_writePage(FLASH_ADDRESS_OFFSET + (flashEndSector * 4096) + (i * 256), SPI_Flash_sectorbuffer + (i * 256));

				if (!retVal)
				{
					goto errorExit;
				}
			}

		}
	}

errorExit:
#if defined(PLATFORM_MD9600)
	if (outOfBandFlag)
	{
		channelBuf->LibreDMR_flag1 |= CODEPLUG_CHANNEL_LIBREDMR_FLAG1_OUT_OF_BAND;
	}
#endif

	// Need to restore the values back to what we need for the operation of the firmware rather than the BCD values the codeplug uses
	channelBuf->chMode = (channelBuf->chMode == 0) ? RADIO_MODE_ANALOG : RADIO_MODE_DIGITAL;
	// Convert the the legacy codeplug tx and rx freq values into normal integers
	channelBuf->txFreq = bcd2int(channelBuf->txFreq);
	channelBuf->rxFreq = bcd2int(channelBuf->rxFreq);
	channelBuf->txTone = codeplugCSSToInt(channelBuf->txTone);
	channelBuf->rxTone = codeplugCSSToInt(channelBuf->rxTone);

	return retVal;
}

static void codeplugRxGroupInitCache(void)
{
	SPI_Flash_read(FLASH_ADDRESS_OFFSET + CODEPLUG_ADDR_RX_GROUP_LEN, (uint8_t*) &codeplugRXGroupCache[0], CODEPLUG_RX_GROUPLIST_MAX);
}

bool codeplugRxGroupGetDataForIndex(int index, struct_codeplugRxGroup_t *rxGroupBuf)
{
	int i = 0;
	struct_codeplugContact_t contactData;

	if ((index >= 1) && (index <= CODEPLUG_RX_GROUPLIST_MAX))
	{
		index--; // Index numbers start from 1 not zero

		if (codeplugRXGroupCache[index] > 0)
		{
			// Not our struct contains an extra property to hold the number of TGs in the group
			SPI_Flash_read(FLASH_ADDRESS_OFFSET + CODEPLUG_ADDR_RX_GROUP + (index * CODEPLUG_RXGROUP_DATA_STRUCT_SIZE), (uint8_t *) rxGroupBuf, CODEPLUG_RXGROUP_DATA_STRUCT_SIZE);

			for (i = 0; i < 32; i++)
			{
				codeplugContactGetDataForIndex(rxGroupBuf->contacts[i], &contactData);
				rxGroupBuf->NOT_IN_CODEPLUG_contactsTG[i] = contactData.tgNumber;
				// Empty groups seem to be filled with zeros
				if (rxGroupBuf->contacts[i] == 0)
				{
					break;
				}
			}

			rxGroupBuf->NOT_IN_CODEPLUG_numTGsInGroup = i;
			return true;
		}
	}

	rxGroupBuf->name[0] = 0;
	rxGroupBuf->NOT_IN_CODEPLUG_numTGsInGroup = 0;
	return false;
}

int codeplugDTMFContactsGetCount(void)
{
	return codeplugContactsCache.numDTMFContacts;
}

int codeplugContactsGetCount(uint32_t callType) // 0:TG 1:PC 2:ALL
{
	switch (callType)
	{
		case CONTACT_CALLTYPE_TG:
			return codeplugContactsCache.numTGContacts;
			break;
		case CONTACT_CALLTYPE_PC:
			return codeplugContactsCache.numPCContacts;
			break;
		case CONTACT_CALLTYPE_ALL:
			return codeplugContactsCache.numALLContacts;
			break;
	}

	return 0; // Should not happen
}

// Returns contact's index, or 0 on failure.
int codeplugDTMFContactGetDataForNumber(int number, struct_codeplugDTMFContact_t *contact)
{
	if ((number >= CODEPLUG_DTMF_CONTACTS_MIN) && (number <= CODEPLUG_DTMF_CONTACTS_MAX))
	{
		if (codeplugDTMFContactGetDataForIndex(codeplugContactsCache.contactsDTMFLookupCache[number - 1].index, contact))
		{
			return codeplugContactsCache.contactsDTMFLookupCache[number - 1].index;
		}
	}

	return 0;
}

// Returns contact's index, or 0 on failure.
int codeplugContactGetDataForNumberInType(int number, uint32_t callType, struct_codeplugContact_t *contact)
{
	int numContacts = codeplugContactsCache.numTGContacts + codeplugContactsCache.numALLContacts + codeplugContactsCache.numPCContacts;

	for (int i = 0; i < numContacts; i++)
	{
		if ((codeplugContactsCache.contactsLookupCache[i].tgOrPCNum >> 24) == callType)
		{
			number--;
		}

		if (number == 0)
		{
			if (codeplugContactGetDataForIndex(codeplugContactsCache.contactsLookupCache[i].index, contact))
			{
				return codeplugContactsCache.contactsLookupCache[i].index;
			}
		}
	}

	return 0;
}

// optionalTS: 0 = no TS checking, 1..2 = TS
int codeplugContactIndexByTGorPCFromNumber(int number, uint32_t tgorpc, uint32_t callType, struct_codeplugContact_t *contact, uint8_t optionalTS)
{
	int numContacts = codeplugContactsCache.numTGContacts + codeplugContactsCache.numALLContacts + codeplugContactsCache.numPCContacts;
	int firstMatch = -1;

	for (int i = number; i < numContacts; i++)
	{
		if (((codeplugContactsCache.contactsLookupCache[i].tgOrPCNum & 0xFFFFFF) == tgorpc) &&
				/* All Call, hence ignore callType */
				((tgorpc == ALL_CALL_VALUE) || ((codeplugContactsCache.contactsLookupCache[i].tgOrPCNum >> 24) == callType)))
		{
			// Check for the contact TS override
			if (optionalTS > 0)
			{
				// Just read the reserve1 byte for now
				codeplugContactGetReserve1ByteForIndex(codeplugContactsCache.contactsLookupCache[i].index, contact);

				if (((contact->reserve1 & CODEPLUG_CONTACT_FLAG_NO_TS_OVERRIDE) == 0x00) && (((contact->reserve1 & CODEPLUG_CONTACT_FLAG_TS_OVERRIDE_TIMESLOT_MASK) >> 1) == (optionalTS - 1)))
				{
					codeplugContactGetDataForIndex(codeplugContactsCache.contactsLookupCache[i].index, contact);
					return i;
				}
				else
				{
					if (firstMatch < 0)
					{
						firstMatch = i;
					}
				}
			}
			else
			{
				codeplugContactGetDataForIndex(codeplugContactsCache.contactsLookupCache[i].index, contact);
				return i;
			}
		}
	}

	if (firstMatch >= 0)
	{
		codeplugContactGetDataForIndex(codeplugContactsCache.contactsLookupCache[firstMatch].index, contact);
		return firstMatch;
	}

	return -1;
}

// optionalTS: 0 = no TS checking, 1..2 = TS
int codeplugContactIndexByTGorPC(uint32_t tgorpc, uint32_t callType, struct_codeplugContact_t *contact, uint8_t optionalTS)
{
	return codeplugContactIndexByTGorPCFromNumber(0, tgorpc, callType, contact, optionalTS);
}

bool codeplugContactsContainsPC(uint32_t pc)
{
	int numContacts =  codeplugContactsCache.numTGContacts + codeplugContactsCache.numALLContacts + codeplugContactsCache.numPCContacts;
	pc = pc & 0x00FFFFFF;
	pc = pc | (CONTACT_CALLTYPE_PC << 24);

	for (int i = 0; i < numContacts; i++)
	{
		if (codeplugContactsCache.contactsLookupCache[i].tgOrPCNum == pc)
		{
			return true;
		}
	}
	return false;
}

static void codeplugInitContactsCache(void)
{
	struct_codeplugContact_t contact;
	uint8_t                  c;
	int                      codeplugNumContacts = 0;

	codeplugContactsCache.numTGContacts = 0;
	codeplugContactsCache.numPCContacts = 0;
	codeplugContactsCache.numALLContacts = 0;
	codeplugContactsCache.numDTMFContacts = 0;

	for(int i = 0; i < CODEPLUG_CONTACTS_MAX; i++)
	{
		if (SPI_Flash_read(FLASH_ADDRESS_OFFSET + (CODEPLUG_ADDR_CONTACTS + (i * CODEPLUG_CONTACT_DATA_SIZE)), (uint8_t *)&contact, 16 + 4 + 1))// Name + TG/ID + Call type
		{
			if (contact.name[0] != 0xFF)
			{
				codeplugContactsCache.contactsLookupCache[codeplugNumContacts].tgOrPCNum = bcd2int(byteSwap32(contact.tgNumber));
				codeplugContactsCache.contactsLookupCache[codeplugNumContacts].index = i + 1;// Contacts are numbered from 1 to 1024
				codeplugContactsCache.contactsLookupCache[codeplugNumContacts].tgOrPCNum |= (contact.callType << 24);// Store the call type in the upper byte
				if (contact.callType == CONTACT_CALLTYPE_PC)
				{
					codeplugContactsCache.numPCContacts++;
				}
				else if (contact.callType == CONTACT_CALLTYPE_TG)
				{
					codeplugContactsCache.numTGContacts++;
				}
				else if (contact.callType == CONTACT_CALLTYPE_ALL)
				{
					codeplugContactsCache.numALLContacts++;
				}

				codeplugNumContacts++;
			}
		}
	}

	for (int i = 0; i < CODEPLUG_DTMF_CONTACTS_MAX; i++)
	{
		if (EEPROM_Read(CODEPLUG_ADDR_DTMF_CONTACTS + (i * CODEPLUG_DTMF_CONTACT_DATA_STRUCT_SIZE), (uint8_t *)&c, 1))
		{
			// Empty DTMF contacts normally begin with 0xFF, but when expanding to use 64 DTMF contacts, the old Zone
			// basic data is in the last contact and this contains 0x00 in the first byte, until the codeplug is updated
			if ((c != 0xFF) && (c != 0x00))
			{
				codeplugContactsCache.contactsDTMFLookupCache[codeplugContactsCache.numDTMFContacts++].index = i + 1; // Contacts are numbered from 1 to 32
			}
		}
	}
}

void codeplugContactsCacheUpdateOrInsertContactAt(int index, struct_codeplugContact_t *contact)
{
	int numContacts =  codeplugContactsCache.numTGContacts + codeplugContactsCache.numALLContacts + codeplugContactsCache.numPCContacts;
	int numContactsMinus1 = numContacts - 1;

	for(int i = 0; i < numContacts; i++)
	{
		// Check if the contact is already in the cache, and is being modified
		if (codeplugContactsCache.contactsLookupCache[i].index == index)
		{
			uint8_t callType = codeplugContactsCache.contactsLookupCache[i].tgOrPCNum >> 24;// get call type from cache

			if (callType != contact->callType)
			{
				switch (callType)
				{
					case CONTACT_CALLTYPE_TG:
						codeplugContactsCache.numTGContacts--;
						break;
					case CONTACT_CALLTYPE_PC:
						codeplugContactsCache.numPCContacts--;
						break;
					case CONTACT_CALLTYPE_ALL:
						codeplugContactsCache.numALLContacts--;
						break;
				}

				switch (contact->callType)
				{
					case CONTACT_CALLTYPE_TG:
						codeplugContactsCache.numTGContacts++;
						break;
					case CONTACT_CALLTYPE_PC:
						codeplugContactsCache.numPCContacts++;
						break;
					case CONTACT_CALLTYPE_ALL:
						codeplugContactsCache.numALLContacts++;
						break;
				}
			}
			//update the
			codeplugContactsCache.contactsLookupCache[i].tgOrPCNum = bcd2int(byteSwap32(contact->tgNumber));
			codeplugContactsCache.contactsLookupCache[i].tgOrPCNum |= (contact->callType << 24);// Store the call type in the upper byte

			return;
		}
		else
		{
			if((i < numContactsMinus1) && (codeplugContactsCache.contactsLookupCache[i].index < index) && (codeplugContactsCache.contactsLookupCache[i + 1].index > index))
			{
				if (contact->callType == CONTACT_CALLTYPE_PC)
				{
					codeplugContactsCache.numPCContacts++;
				}
				else if (contact->callType == CONTACT_CALLTYPE_TG)
				{
					codeplugContactsCache.numTGContacts++;
				}
				else if (contact->callType == CONTACT_CALLTYPE_ALL)
				{
					codeplugContactsCache.numALLContacts++;
				}

				numContacts++;// Total contacts increases by 1

				// Note . Need to use memmove as the source and destination overlap.
				memmove(&codeplugContactsCache.contactsLookupCache[i + 2], &codeplugContactsCache.contactsLookupCache[i + 1], (numContacts - 2 - i) * sizeof(codeplugContactCache_t));

				codeplugContactsCache.contactsLookupCache[i + 1].tgOrPCNum = bcd2int(byteSwap32(contact->tgNumber));
				codeplugContactsCache.contactsLookupCache[i + 1].index = index;// Contacts are numbered from 1 to 1024
				codeplugContactsCache.contactsLookupCache[i + 1].tgOrPCNum |= (contact->callType << 24);// Store the call type in the upper byte
				return;
			}
		}
	}

	// Did not find the index in the cache or a gap between 2 existing indexes. So the new contact needs to be added to the end of the cache

	if (contact->callType == CONTACT_CALLTYPE_PC)
	{
		codeplugContactsCache.numPCContacts++;
	}
	else if (contact->callType == CONTACT_CALLTYPE_TG)
	{
		codeplugContactsCache.numTGContacts++;
	}
	else if (contact->callType == CONTACT_CALLTYPE_ALL)
	{
		codeplugContactsCache.numALLContacts++;
	}

	// Note. We can use numContacts as the the index as the array is zero indexed but the number of contacts is starts from 1
	// Hence is already in some ways pre incremented in terms of being an array index
	codeplugContactsCache.contactsLookupCache[numContacts].tgOrPCNum = bcd2int(byteSwap32(contact->tgNumber));
	codeplugContactsCache.contactsLookupCache[numContacts].index = index;// Contacts are numbered from 1 to 1024
	codeplugContactsCache.contactsLookupCache[numContacts].tgOrPCNum |= (contact->callType << 24);// Store the call type in the upper byte
}

void codeplugContactsCacheRemoveContactAt(int index)
{
	int numContacts = codeplugContactsCache.numTGContacts + codeplugContactsCache.numALLContacts + codeplugContactsCache.numPCContacts;
	for(int i = 0; i < numContacts; i++)
	{
		if(codeplugContactsCache.contactsLookupCache[i].index == index)
		{
			uint8_t callType = codeplugContactsCache.contactsLookupCache[i].tgOrPCNum >> 24;

			if (callType == CONTACT_CALLTYPE_PC)
			{
				codeplugContactsCache.numPCContacts--;
			}
			else if (callType == CONTACT_CALLTYPE_TG)
			{
				codeplugContactsCache.numTGContacts--;
			}
			else if (callType == CONTACT_CALLTYPE_ALL)
			{
				codeplugContactsCache.numALLContacts--;
			}
			// Note memcpy should work here, because memcpy normally copys from the lowest memory location upwards
			memcpy(&codeplugContactsCache.contactsLookupCache[i], &codeplugContactsCache.contactsLookupCache[i + 1], (numContacts - 1 - i) * sizeof(codeplugContactCache_t));
			return;
		}
	}
}

uint32_t codeplugContactGetPackedId(struct_codeplugContact_t *contact)
{
	return ((contact->callType == CONTACT_CALLTYPE_PC) ? (contact->tgNumber | (PC_CALL_FLAG << 24)) : contact->tgNumber);
}

int codeplugContactGetFreeIndex(void)
{
	int numContacts = codeplugContactsCache.numTGContacts + codeplugContactsCache.numALLContacts + codeplugContactsCache.numPCContacts;
	int lastIndex = 0;
	int i;

	for (i = 0; i < numContacts; i++)
	{
		if (codeplugContactsCache.contactsLookupCache[i].index != lastIndex + 1)
		{
			return lastIndex + 1;
		}
		lastIndex = codeplugContactsCache.contactsLookupCache[i].index;
	}

	if (i < CODEPLUG_CONTACTS_MAX)
	{
		return codeplugContactsCache.contactsLookupCache[i - 1].index + 1;
	}

	return 0;
}

bool codeplugDTMFContactGetDataForIndex(int index, struct_codeplugDTMFContact_t *contact)
{
	if ((codeplugContactsCache.numDTMFContacts > 0) &&  (index >= CODEPLUG_DTMF_CONTACTS_MIN) && (index <= CODEPLUG_DTMF_CONTACTS_MAX))
	{
		index--;
		if(EEPROM_Read(CODEPLUG_ADDR_DTMF_CONTACTS + (index * CODEPLUG_DTMF_CONTACT_DATA_STRUCT_SIZE), (uint8_t *)contact, CODEPLUG_DTMF_CONTACT_DATA_STRUCT_SIZE))
		{
			return true;
		}
	}

	memset(contact, 0xff, CODEPLUG_DTMF_CONTACT_DATA_STRUCT_SIZE);
	return false;
}

static bool codeplugContactGetReserve1ByteForIndex(int index, struct_codeplugContact_t *contact)
{
	if (((codeplugContactsCache.numTGContacts > 0) || (codeplugContactsCache.numPCContacts > 0) || (codeplugContactsCache.numALLContacts > 0)) &&
			(index >= CODEPLUG_CONTACTS_MIN) && (index <= CODEPLUG_CONTACTS_MAX))
	{
		index--;
		SPI_Flash_read(FLASH_ADDRESS_OFFSET + CODEPLUG_ADDR_CONTACTS + (index * CODEPLUG_CONTACT_DATA_SIZE) + 23 , (uint8_t *)contact + 23, 1);
		return true;
	}
	return false;
}

bool codeplugContactGetDataForIndex(int index, struct_codeplugContact_t *contact)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];

	if (((codeplugContactsCache.numTGContacts > 0) || (codeplugContactsCache.numPCContacts > 0) || (codeplugContactsCache.numALLContacts > 0)) &&
			(index >= CODEPLUG_CONTACTS_MIN) && (index <= CODEPLUG_CONTACTS_MAX))
	{
		index--;
		SPI_Flash_read(FLASH_ADDRESS_OFFSET + CODEPLUG_ADDR_CONTACTS + index * CODEPLUG_CONTACT_DATA_SIZE, (uint8_t *)contact, CODEPLUG_CONTACT_DATA_SIZE);
		contact->NOT_IN_CODEPLUGDATA_indexNumber = index + 1;
		contact->tgNumber = bcd2int(byteSwap32(contact->tgNumber));
		return true;
	}

	// If an invalid contact number has been requested, return a TG 9 contact
	contact->tgNumber = 9;
	contact->callType = CONTACT_CALLTYPE_TG;
	contact->reserve1 = 0xff;
	contact->NOT_IN_CODEPLUGDATA_indexNumber = -1;
	snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s 9", currentLanguage->tg);
	codeplugUtilConvertStringToBuf(buf, contact->name, 16);
	return false;
}

int codeplugContactSaveDataForIndex(int index, struct_codeplugContact_t *contact)
{
	int retVal;
	int flashWritePos = CODEPLUG_ADDR_CONTACTS;
	int flashSector;
	int flashEndSector;
	int bytesToWriteInCurrentSector = CODEPLUG_CONTACT_DATA_SIZE;
	uint32_t unconvertedTgNumber = contact->tgNumber;

	index--;
	contact->tgNumber = byteSwap32(int2bcd(contact->tgNumber));

	flashWritePos += index * CODEPLUG_CONTACT_DATA_SIZE;// go to the position of the specific index

	flashSector 	= flashWritePos / 4096;
	flashEndSector 	= (flashWritePos + CODEPLUG_CONTACT_DATA_SIZE) / 4096;

	if (flashSector != flashEndSector)
	{
		bytesToWriteInCurrentSector = (flashEndSector * 4096) - flashWritePos;
	}

	SPI_Flash_read(FLASH_ADDRESS_OFFSET + (flashSector * 4096), SPI_Flash_sectorbuffer, 4096);
	uint8_t *writePos = SPI_Flash_sectorbuffer + flashWritePos - (flashSector * 4096);
	memcpy(writePos, contact, bytesToWriteInCurrentSector);

	retVal = SPI_Flash_eraseSector(FLASH_ADDRESS_OFFSET + (flashSector * 4096));
	if (!retVal)
	{
		goto hasFailed;
	}

	for (int i = 0; i < 16; i++)
	{
		retVal = SPI_Flash_writePage(FLASH_ADDRESS_OFFSET + (flashSector * 4096) + (i * 256), SPI_Flash_sectorbuffer + (i * 256));
		if (!retVal)
		{
			goto hasFailed;
		}
	}

	if (flashSector != flashEndSector)
	{
		uint8_t *channelBufPusOffset = (uint8_t *)contact + bytesToWriteInCurrentSector;
		bytesToWriteInCurrentSector = CODEPLUG_CONTACT_DATA_SIZE - bytesToWriteInCurrentSector;

		SPI_Flash_read(FLASH_ADDRESS_OFFSET + (flashEndSector * 4096), SPI_Flash_sectorbuffer, 4096);
		memcpy(SPI_Flash_sectorbuffer, (uint8_t *)channelBufPusOffset, bytesToWriteInCurrentSector);

		retVal = SPI_Flash_eraseSector(FLASH_ADDRESS_OFFSET + (flashEndSector * 4096));

		if (!retVal)
		{
			goto hasFailed;
		}

		for (int i = 0; i < 16; i++)
		{
			retVal = SPI_Flash_writePage(FLASH_ADDRESS_OFFSET + (flashEndSector * 4096) + (i * 256), SPI_Flash_sectorbuffer + (i * 256));

			if (!retVal)
			{
				goto hasFailed;
			}
		}

	}
	if ((contact->name[0] == 0xff) || (contact->callType == 0xFF))
	{
		codeplugContactsCacheRemoveContactAt(index + 1);// index was decremented at the start of the function
	}
	else
	{
		codeplugContactsCacheUpdateOrInsertContactAt(index + 1, contact);
		//initCodeplugContactsCache();// Update the cache
	}


	hasFailed:

	// Restore contact's TG number
	contact->tgNumber = unconvertedTgNumber;

	return retVal;
}

bool codeplugContactGetRXGroup(int index)
{
	struct_codeplugRxGroup_t rxGroupBuf;
	int i;

	for (i = 1; i <= CODEPLUG_RX_GROUPLIST_MAX; i++)
	{
		if (codeplugRxGroupGetDataForIndex(i, &rxGroupBuf))
		{
			for (int j = 0; j < 32; j++)
			{
				if (rxGroupBuf.contacts[j] == index)
				{
					return true;
				}
			}
		}
	}
	return false;
}

uint32_t codeplugGetUserDMRID(void)
{
	uint32_t dmrId;
	EEPROM_Read(CODEPLUG_ADDR_USER_DMRID, (uint8_t *)&dmrId, 4);
	return bcd2int(byteSwap32(dmrId));
}

void codeplugSetUserDMRID(uint32_t dmrId)
{
	dmrId = byteSwap32(int2bcd(dmrId));
	EEPROM_Write(CODEPLUG_ADDR_USER_DMRID, (uint8_t *)&dmrId, 4);
}

// Max length the user can enter is 8. Hence buf must be 16 chars to allow for the termination
void codeplugGetRadioName(char *buf)
{
	memset(buf, 0, 9);
	EEPROM_Read(CODEPLUG_ADDR_USER_CALLSIGN, (uint8_t *)buf, 8);
	codeplugUtilConvertBufToString(buf, buf, 8);
}

// Max length the user can enter is 16. Hence buf must be 17 chars to allow for the termination
void codeplugGetBootScreenData(char *line1, char *line2, uint8_t *displayType)
{
	memset(line1, 0, SCREEN_LINE_BUFFER_SIZE);
	memset(line2, 0, SCREEN_LINE_BUFFER_SIZE);

	EEPROM_Read(CODEPLUG_ADDR_BOOT_LINE1, (uint8_t *)line1, (SCREEN_LINE_BUFFER_SIZE - 1));
	codeplugUtilConvertBufToString(line1, line1, (SCREEN_LINE_BUFFER_SIZE - 1));
	EEPROM_Read(CODEPLUG_ADDR_BOOT_LINE2, (uint8_t *)line2, (SCREEN_LINE_BUFFER_SIZE - 1));
	codeplugUtilConvertBufToString(line2, line2, (SCREEN_LINE_BUFFER_SIZE - 1));

	EEPROM_Read(CODEPLUG_ADDR_BOOT_INTRO_SCREEN, displayType, 1);// read the display type
}

void codeplugGetVFO_ChannelData(struct_codeplugChannel_t *vfoBuf, Channel_t VFONumber)
{
	EEPROM_Read(CODEPLUG_ADDR_VFO_A_CHANNEL + (CODEPLUG_CHANNEL_DATA_STRUCT_SIZE * (int)VFONumber), (uint8_t *)vfoBuf, CODEPLUG_CHANNEL_DATA_STRUCT_SIZE);

	// Convert the the legacy codeplug tx and rx freq values into normal integers
	vfoBuf->chMode = (vfoBuf->chMode == 0) ? RADIO_MODE_ANALOG : RADIO_MODE_DIGITAL;
	vfoBuf->txFreq = bcd2int(vfoBuf->txFreq);
	vfoBuf->rxFreq = bcd2int(vfoBuf->rxFreq);
	vfoBuf->txTone = codeplugCSSToInt(vfoBuf->txTone);
	vfoBuf->rxTone = codeplugCSSToInt(vfoBuf->rxTone);
	vfoBuf->NOT_IN_CODEPLUG_flag = ((VFONumber == CHANNEL_VFO_A) ? 0x01 : ((VFONumber == CHANNEL_VFO_B) ? 0x03 : 0x00));
	vfoBuf->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 = -1;

	if (vfoBuf->sql > 21U)
	{
		vfoBuf->sql = 10U;
	}
}

void codeplugSetVFO_ChannelData(struct_codeplugChannel_t *vfoBuf, Channel_t VFONumber)
{
	struct_codeplugChannel_t tmpChannel;

	memcpy(&tmpChannel, vfoBuf, CODEPLUG_CHANNEL_DATA_STRUCT_SIZE);// save current VFO data as we need to modify

	codeplugConvertChannelInternalToCodeplug(&tmpChannel, vfoBuf);

	EEPROM_Write(CODEPLUG_ADDR_VFO_A_CHANNEL + (CODEPLUG_CHANNEL_DATA_STRUCT_SIZE * (int)VFONumber), (uint8_t *)&tmpChannel, CODEPLUG_CHANNEL_DATA_STRUCT_SIZE);
}

void codeplugConvertChannelInternalToCodeplug(struct_codeplugChannel_t *codeplugChannel, struct_codeplugChannel_t *internalChannel)
{
	codeplugChannel->chMode = (internalChannel->chMode == RADIO_MODE_ANALOG) ? 0 : 1;
	codeplugChannel->txFreq = int2bcd(internalChannel->txFreq);
	codeplugChannel->rxFreq = int2bcd(internalChannel->rxFreq);
	codeplugChannel->txTone = codeplugIntToCSS(internalChannel->txTone);
	codeplugChannel->rxTone = codeplugIntToCSS(internalChannel->rxTone);
}

void codeplugInitChannelsPerZone(void)
{
	uint8_t buf[16];
	// 0x806F is the last byte of the name of the second Zone if 16 channels per zone
	// And because the CPS terminates the zone name with 0xFF, this will be 0xFF if its a 16 channel per zone schema
	// If the zone has 80 channels per zone this address will be the upper byte of the channel number and because the max channels is 1024
	// this value can never me 0xff if its a channel number

	// Note. I tried to read just 1 byte but it crashed. So I am now reading 16 bytes and checking the last one
	EEPROM_Read(0x8060, (uint8_t *)buf, 16);
	if ((buf[15] >= 0x00) && (buf[15] <= 0x04))
	{
		codeplugChannelsPerZone = 80;// Must be the new 80 channel per zone format
	}
}

static uint32_t codeplugGetOpenGD77CustomDataStartAddressForType(codeplugCustomDataType_t dataType, codeplugCustomDataBlockHeader_t *blockHeader)
{
	const uint32_t MAX_BLOCK_ADDRESS = 0x10000;
	uint32_t dataHeaderAddress = 12;
	uint8_t tmpBuf[12];

	SPI_Flash_read(FLASH_ADDRESS_OFFSET + 0, tmpBuf, 12);

	if (memcmp("OpenGD77", tmpBuf, 8) == 0)
	{
		do
		{
			SPI_Flash_read(FLASH_ADDRESS_OFFSET + dataHeaderAddress, (uint8_t *)blockHeader, sizeof(codeplugCustomDataBlockHeader_t));

			if (blockHeader->dataType == dataType)
			{
				return dataHeaderAddress;
			}

			if ((blockHeader->dataLength == 0) || (blockHeader->dataLength == 0xFFFFFFFF))
			{
				return 0;
			}

			dataHeaderAddress += sizeof(codeplugCustomDataBlockHeader_t) + blockHeader->dataLength;

		} while (dataHeaderAddress < MAX_BLOCK_ADDRESS);
	}

	return 0;
}

static uint32_t codeplugGetOpenGD77CustomDataFirstEmptySlot(int len)
{
	const uint32_t MAX_BLOCK_ADDRESS = 0x10000;
	codeplugCustomDataBlockHeader_t blockHeader;
	uint32_t dataHeaderAddress;

	if ((dataHeaderAddress = codeplugGetOpenGD77CustomDataStartAddressForType(CODEPLUG_CUSTOM_DATA_TYPE_EMPTY, &blockHeader)) > 0)
	{
		if ((blockHeader.dataLength == 0xFFFFFFFF) &&
				((dataHeaderAddress + sizeof(codeplugCustomDataBlockHeader_t) + len) < MAX_BLOCK_ADDRESS))
		{
			return dataHeaderAddress;
		}
	}

	return 0;
}

bool codeplugGetOpenGD77CustomData(codeplugCustomDataType_t dataType, uint8_t *dataBuf)
{
	codeplugCustomDataBlockHeader_t blockHeader;
	uint32_t dataHeaderAddress;

	if ((dataHeaderAddress = codeplugGetOpenGD77CustomDataStartAddressForType(dataType, &blockHeader)) > 0)
	{
		SPI_Flash_read(FLASH_ADDRESS_OFFSET + dataHeaderAddress + sizeof(codeplugCustomDataBlockHeader_t), dataBuf, blockHeader.dataLength);
		return true;
	}

	return false;
}

bool codeplugSetOpenGD77CustomData(codeplugCustomDataType_t dataType, uint8_t *dataBuf, int len)
{
	codeplugCustomDataBlockHeader_t blockHeader;
	uint32_t dataHeaderAddress;

	if ((dataHeaderAddress = codeplugGetOpenGD77CustomDataStartAddressForType(dataType, &blockHeader)) == 0)
	{
		if ((dataHeaderAddress = codeplugGetOpenGD77CustomDataFirstEmptySlot(len)) == 0)
		{
			return false;
		}

		// We got a location for this new storage, validate the header.
		blockHeader.dataType = dataType;
		blockHeader.dataLength = len;
	}

	if (blockHeader.dataLength == len) // it's not permitted to change the block size, it has to be equal.
	{
		SPI_Flash_write(FLASH_ADDRESS_OFFSET + dataHeaderAddress, (uint8_t *)&blockHeader, sizeof(codeplugCustomDataBlockHeader_t));
		SPI_Flash_write(FLASH_ADDRESS_OFFSET + dataHeaderAddress + sizeof(codeplugCustomDataBlockHeader_t), dataBuf, len);
		return true;
	}

	return false;
}

bool codeplugGetGeneralSettings(struct_codeplugGeneralSettings_t *generalSettingsBuffer)
{
	return EEPROM_Read(CODEPLUG_ADDR_GENERAL_SETTINGS, (uint8_t *)generalSettingsBuffer, CODEPLUG_GENERAL_SETTINGS_DATA_STRUCT_SIZE);
}

bool codeplugGetSignallingDTMF(struct_codeplugSignalling_DTMF_t *signallingDTMFBuffer)
{
	return EEPROM_Read(CODEPLUG_ADDR_SIGNALLING_DTMF, (uint8_t *)signallingDTMFBuffer, CODEPLUG_SIGNALLING_DTMF_DATA_STRUCT_SIZE);
}

bool codeplugGetSignallingDTMFDurations(struct_codeplugSignalling_DTMFDurations_t *signallingDTMFDurationsBuffer)
{
	if (EEPROM_Read(CODEPLUG_ADDR_SIGNALLING_DTMF_DURATIONS, (uint8_t *)signallingDTMFDurationsBuffer, SIGNALLING_DTMF_DURATIONS_DATA_STRUCT_SIZE))
	{
		// Default codeplug value was 128(0x80). Override any value above 1000ms
		if (signallingDTMFDurationsBuffer->libreDMR_Tail > 10)
		{
			signallingDTMFDurationsBuffer->libreDMR_Tail = 5;
		}

		return true;
	}
	else
	{
		// Avoid division-by-zero
		signallingDTMFDurationsBuffer->rate = 2; // 250ms/250ms
	}

	return false;
}


bool codeplugGetDeviceInfo(struct_codeplugDeviceInfo_t *deviceInfoBuffer)
{
	bool readOK = EEPROM_Read(CODEPLUG_ADDR_DEVICE_INFO, (uint8_t *)deviceInfoBuffer, sizeof(struct_codeplugDeviceInfo_t));//CODEPLUG_ADDR_DEVICE_INFO_READ_SIZE);
	if (readOK)
	{
		deviceInfoBuffer->minUHFFreq = bcd2uint16(deviceInfoBuffer->minUHFFreq);
		deviceInfoBuffer->maxUHFFreq = bcd2uint16(deviceInfoBuffer->maxUHFFreq);

		deviceInfoBuffer->minVHFFreq = bcd2uint16(deviceInfoBuffer->minVHFFreq);
		deviceInfoBuffer->maxVHFFreq = bcd2uint16(deviceInfoBuffer->maxVHFFreq);

		return true;
	}
	return false;
}

static void codeplugQuickKeyInitCache(void)
{
	EEPROM_Read(CODEPLUG_ADDR_QUICKKEYS, (uint8_t *)&quickKeysCache, (CODEPLUG_QUICKKEYS_SIZE * sizeof(uint16_t)));
}

uint16_t codeplugGetQuickkeyFunctionID(char key)
{
	uint16_t functionId = 0;

	if ((key >= '0') && (key <= '9'))
	{
		key = key - '0';
		functionId = quickKeysCache[(int) key];
	}

	return functionId;
}

static bool quickkeyIsEmpty(char key, uint16_t functionId)
{
	// Wants to clear a slot, so consider it empty
	if (functionId == 0x8000)
	{
		return true;
	}

	uint16_t data = codeplugGetQuickkeyFunctionID(key);

	if ((data == 0x8000) ||
			(((data & 0x8000) == 0) && ((data < CODEPLUG_CONTACTS_MIN) || (data > CODEPLUG_CONTACTS_MAX))))
	{
		return true;
	}

	return false;
}

bool codeplugSetQuickkeyFunctionID(char key, uint16_t functionId)
{
	// Only permit to store QuickKey in empty slots
	if (quickkeyIsEmpty(key, functionId) && (key >= '0' && key <= '9'))
	{
		key = key - '0';
		EEPROM_Write(CODEPLUG_ADDR_QUICKKEYS + (sizeof(uint16_t) * (int)key), (uint8_t *)&functionId, sizeof(uint16_t));
		quickKeysCache[(int) key] = functionId;

		return true;
	}

	return false;
}

int codeplugGetRepeaterWakeAttempts(void)
{
	return 4;// Hard coded. In the future we may read this from the codeplug.
}

static void codeplugAPRSInitCache(void)
{
	codeplugAPRS_Config_t aprsBuff;
	int aprsIdx = 1;

	do
	{
		memset(&aprsBuff, 0x00, sizeof(codeplugAPRS_Config_t));

		codeplugAPRSCache.numOfConfigs = aprsIdx; // Cheating the read function

		if (codeplugAPRSGetDataForIndex(aprsIdx, &aprsBuff) == false)
		{
			break;
		}

		aprsIdx++;
		codeplugAPRSCache.numOfConfigs++;

	} while(aprsIdx <= 8);

	codeplugAPRSCache.numOfConfigs = (aprsIdx - 1);
}

void codeplugInitCaches(void)
{
	codeplugInitContactsCache();

	codeplugAllChannelsInitCache();

	codeplugZonesInitCache();
	codeplugRxGroupInitCache();
	codeplugQuickKeyInitCache();

	codeplugInitLastUsedChannelInZone();

	codeplugAPRSInitCache();
}

// Returns pin length or 0 if no pin. Pin code is passed as pointer to int32_t
int codeplugGetPasswordPin(int32_t *pinCode)
{
	int pinLength = 0;
	uint8_t buf[6];

	if (EEPROM_Read(CODEPLUG_ADDR_BOOT_PASSWORD_PIN + 1, (uint8_t *)buf, 6))
	{
		if ((buf[0] & 0x01) == 0)
		{
			return pinLength;
		}

		uint8_t nibble;
		*pinCode = 0;
		for(int i = 0 ; i < 6 ; i++)
		{
			nibble = buf[(i/2)+3] >> ((1-(i % 2)) * 4) & 0x0F;
			if (nibble != 0x0f)
			{
				*pinCode = (*pinCode * 10) + nibble;
				pinLength++;
			}
			else
			{
				break;
			}
		}
	}
	return pinLength;
}

void codeplugSetTATxForTS(struct_codeplugChannel_t *channelBuf, uint8_t ts, taTxEnum_t taTxValue)
{
	bool isTS1 = (ts == 0);

	channelBuf->flag1 &= (isTS1 ? 0xFC : 0xF3);
	channelBuf->flag1 |= (taTxValue << (isTS1 ? 0 : 2));
}

taTxEnum_t codeplugGetTATxForTS(struct_codeplugChannel_t *channelBuf, uint8_t ts)
{
	return (channelBuf->flag1 >> ((ts == 0) ? 0 : 2)) & 0x03;
}

void codeplugInitLastUsedChannelInZone(void)
{
#if ! defined(PLATFORM_RD5R)
	uint8_t tmpBuf[4];

	EEPROM_Read(CODEPLUG_ADDR_LUCZ, tmpBuf, 4);

	if (memcmp("LUCZ", tmpBuf, 4) == 0) // Check for the header tag
	{
		EEPROM_Read((CODEPLUG_ADDR_LUCZ + 4), (uint8_t *)&lastUsedChannelInZoneData, sizeof(lastUsedChannelInZoneData));
	}
	else
#endif
	{
		// There is no CustomData available, set all last channels to 0
		memset(&lastUsedChannelInZoneData, 0, sizeof(lastUsedChannelInZoneData));

#if defined(PLATFORM_RD5R)
		codeplugSetLastUsedChannelInZone(-1, nonVolatileSettings.currentChannelIndexInAllZone);
		codeplugSetLastUsedChannelInZone(nonVolatileSettings.currentZone, nonVolatileSettings.currentChannelIndexInZone);
#endif
	}
}

int16_t codeplugGetLastUsedChannelInZone(int zoneNum)
{
	int offset = SAFE_MIN(zoneNum, (CODEPLUG_ALL_ZONES_MAX - 1));
	uint8_t *p = (uint8_t *)&lastUsedChannelInZoneData[offset];

	return ((zoneNum >= 0) ? ((int16_t)*p) : (*((int16_t *)p) + 1));
}

int16_t codeplugGetLastUsedChannelInCurrentZone(void)
{
	return (codeplugGetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber));
}

int16_t codeplugGetLastUsedChannelNumberInCurrentZone(void)
{
	int16_t index = codeplugGetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber);

	return ((currentZone.NOT_IN_CODEPLUGDATA_indexNumber >= 0) ? currentZone.channels[index] : index);
}

int16_t codeplugSetLastUsedChannelInZone(int zoneNum, int16_t channelNum)
{
	int offset = SAFE_MIN(zoneNum, (CODEPLUG_ALL_ZONES_MAX - 1));
	uint8_t *p = (uint8_t *)&lastUsedChannelInZoneData[offset];

	if (zoneNum >= 0) // Channels 0..79
	{
		*p = (uint8_t)channelNum;
#if defined(PLATFORM_RD5R)
		settingsSet(nonVolatileSettings.currentChannelIndexInZone, (int16_t) channelNum);
#endif
	}
	else // AllChannels (channels 1..1024)
	{
		*((int16_t *)p) = (channelNum - 1);
#if defined(PLATFORM_RD5R)
		settingsSet(nonVolatileSettings.currentChannelIndexInAllZone, (int16_t) channelNum);
#endif
	}

	lastUsedChannelInZoneHasChanged = true;

	return channelNum;
}

int16_t codeplugSetLastUsedChannelInCurrentZone(int16_t channelNum)
{
	return (codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, channelNum));
}

bool codeplugSaveLastUsedChannelInZone(void)
{
#if ! defined(PLATFORM_RD5R)
	if (lastUsedChannelInZoneHasChanged)
	{
		uint8_t header[4] = { 'L', 'U', 'C', 'Z' };

		lastUsedChannelInZoneHasChanged = ! (EEPROM_Write(CODEPLUG_ADDR_LUCZ, (uint8_t *)&header, 4) &&
				EEPROM_Write(CODEPLUG_ADDR_LUCZ + 4,
						(uint8_t *)&lastUsedChannelInZoneData, sizeof(lastUsedChannelInZoneData)));

		return !lastUsedChannelInZoneHasChanged;
	}
#endif

	return true;
}

int codeplugAPRSConfigGetCount(void)
{
	return codeplugAPRSCache.numOfConfigs;
}

bool codeplugAPRSGetDataForIndex(int index, codeplugAPRS_Config_t *APRS_Buf)
{
	if ((index > 0) && (index <= codeplugAPRSCache.numOfConfigs))
	{
		index--;
		EEPROM_Read(CODEPLUG_ADDR_APRS_CONFIGS + (index * sizeof(codeplugAPRS_Config_t)), (uint8_t *)APRS_Buf, sizeof(codeplugAPRS_Config_t));
	}
	else
	{
		memset(APRS_Buf, 0x00, sizeof(codeplugAPRS_Config_t));
	}

	return !((APRS_Buf->name[0] == 0xFF) || (APRS_Buf->magicVer != APRS_MAGIC_VERSION));
}

int codeplugAPRSGetIndexOfName(char *configName)
{
	uint8_t APRSConfigName[8];
	char convertedConfigName[8];

	codeplugUtilConvertStringToBuf(configName, convertedConfigName, 8);

	for(int i = 0; i < codeplugAPRSCache.numOfConfigs; i++)
	{
		if (!EEPROM_Read(CODEPLUG_ADDR_APRS_CONFIGS + (i * sizeof(codeplugAPRS_Config_t)), (uint8_t *) &APRSConfigName, sizeof(APRSConfigName)))
		{
			return 0;
		}

		if (memcmp(APRSConfigName, convertedConfigName, 8) == 0)
		{
			return (i + 1);
		}
	}

	return 0;
}
