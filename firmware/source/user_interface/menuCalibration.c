/*
 * Copyright (C) 2019-2022 Roger Clark, VK3KYY / G4KYF
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
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"
#include "interfaces/wdog.h"
#include "utils.h"
#include "functions/rxPowerSaving.h"


static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);
static void setRefOscTemp(int16_t cal);
static void setDACTemp(uint8_t freqIndex, uint8_t powerIndex);
static void terminateTransmission(void);
static void applySettings(bool sk2Down);
static void exitCallback(void *data);


#define NO_OF_CAL_FREQS 24
static uint8_t freqIndex = 0;
const uint32_t calFreq[NO_OF_CAL_FREQS] =
{
		13500000, 14000000, 14500000, 15000000, 15500000, 16000000, 16500000, 17000000,
		40000000, 40500000, 41000000, 41500000, 42000000, 42500000, 43000000, 43500000,
		44000000, 44500000, 45000000, 45500000, 46000000, 46500000, 47000000, 47500000
};

#define NO_OF_POWER_LEVELS 2
static uint8_t powerIndex = 0;

const  uint8_t calPower[NO_OF_POWER_LEVELS] = { 1, 5 };			//power calibration is done at the 250mW 1W 2W and 4W levels

static int16_t oscTune[2];						//VHF and UHF oscillator tuning values
static uint8_t powerSetting[NO_OF_CAL_FREQS][NO_OF_POWER_LEVELS];
static bool calIsTransmitting = false;

static menuStatus_t menuOptionsExitCode = MENU_STATUS_SUCCESS;
enum
{
	CALIBRATION_MENU_CAL_FREQUENCY = 0U,
	CALIBRATION_MENU_POWER_LEVEL,
	CALIBRATION_MENU_POWER_SET,
	CALIBRATION_MENU_REF_OSC_VHF,
	CALIBRATION_MENU_REF_OSC_UHF,
	CALIBRATION_MENU_FACTORY,
	NUM_CALIBRATION_MENU_ITEMS
};

typedef enum
{
	CALIBRATION_MENU_PAGE_POWER = 0,
	CALIBRATION_MENU_CAL_PAGE_FREQUENCY_VHF,
	CALIBRATION_MENU_CAL_PAGE_FREQUENCY_UHF,
	CALIBRATION_MENU_PAGE_FACTORY_RESET,
	NUM_CALIBRATION_MENU_PAGES
} CalibrationMenuPageList_t;

static const int PAGE_ITEMS_OFFSET[] =
{
		CALIBRATION_MENU_CAL_FREQUENCY,
		CALIBRATION_MENU_REF_OSC_VHF,
		CALIBRATION_MENU_REF_OSC_UHF,
		CALIBRATION_MENU_FACTORY
};

static CalibrationMenuPageList_t pageNumber = CALIBRATION_MENU_PAGE_POWER;
static int numOptionsOnCurrentPage;


static uint8_t getCalPower(uint8_t level)
{
#if defined(PLATFORM_MDUV380) && !defined(PLATFORM_VARIANT_UV380_PLUS_10W)
	return calPower[(settingsIsOptionBitSet(BIT_FORCE_10W_RADIO) ? 1 : 0)][level];
#else
	return calPower[level];
#endif
}

//#define HAS_DUMPS_CALIBRATION_TABLE 1
#if defined(HAS_DUMPS_CALIBRATION_TABLE)
static void dumpCalibrationTable(void)
{
	USB_DEBUG_printf("--Power Calibration Table---\n");

	for (int f = 0; f < NO_OF_CAL_FREQS; f++)
	{
		int val_before_dp = calFreq[f] / 100000;
		int val_after_dp = (calFreq[f] - (val_before_dp * 100000)) / 100;

		USB_DEBUG_printf("%d.%03d MHz\n", val_before_dp, val_after_dp);

		for (int p = 0; p < NO_OF_POWER_LEVELS; p++)
		{
			USB_DEBUG_printf("\t%u%s: %u\n", getCalPower(p), ((p == 0) ? "mW" : "W"), powerSetting[f][p]);
		}

		vTaskDelay((10 / portTICK_PERIOD_MS));
	}

	USB_DEBUG_printf("--145 MHz Frequency Calibration--\n");
	USB_DEBUG_printf("\t%d\n", oscTune[0]);
	USB_DEBUG_printf("--435 MHz Frequency Calibration--\n");
	USB_DEBUG_printf("\t%d\n", oscTune[1]);
}
#endif

menuStatus_t menuCalibration(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.menuOptionsSetQuickkey = 0;
		menuDataGlobal.menuOptionsTimeout = 0;
		menuDataGlobal.newOptionSelected = true;
		menuDataGlobal.numItems = NUM_CALIBRATION_MENU_ITEMS;

		//get the current calibration values from calibration.c
		oscTune[0] = calibrationGetVHFOscTune();
		oscTune[1] = calibrationGetUHFOscTune();
		for(int i = 0; i < NO_OF_CAL_FREQS; i++)
		{
			for(int j = 0; j < NO_OF_POWER_LEVELS; j++)
			{
				powerSetting[i][j] = calibrationGetPower(i, j);
			}
		}

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->calibration);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		menuSystemRegisterExitCallback(exitCallback, NULL);

		updateScreen(true);

		trxSetFrequency(calFreq[freqIndex], calFreq[freqIndex], DMR_MODE_AUTO);
		trxSetModeAndBandwidth(RADIO_MODE_ANALOG, true);

		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuOptionsExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent || (menuDataGlobal.menuOptionsTimeout > 0))
		{
			handleEvent(ev);
		}
	}
	return menuOptionsExitCode;
}



static void updateScreen(bool isFirstRun)
{
	int mNum = 0;
	char buf[SCREEN_LINE_BUFFER_SIZE];
	const char *leftSide = NULL;// initialize to please the compiler
	const char *rightSideConst = NULL;// initialize to please the compiler
	char rightSideVar[SCREEN_LINE_BUFFER_SIZE];
	voicePrompt_t rightSideUnitsPrompt;
	const char *rightSideUnitsStr = NULL;
	int firstline = 0;
	int lastline = 0;
	int menuNumberOffset = 0;
	int focusNumberOffset = 0;

	displayClearBuf();
	snprintf(buf,SCREEN_LINE_BUFFER_SIZE,"%s %d/%d", currentLanguage->calibration, pageNumber+1 , NUM_CALIBRATION_MENU_PAGES);
	bool settingOption = uiQuickKeysShowChoices(buf, SCREEN_LINE_BUFFER_SIZE, buf);

	switch(pageNumber)
	{
		case CALIBRATION_MENU_PAGE_POWER:
			firstline = CALIBRATION_MENU_CAL_FREQUENCY;
			lastline = CALIBRATION_MENU_POWER_SET;
			numOptionsOnCurrentPage = 3;
			menuNumberOffset = 1;
			focusNumberOffset = 0;
			break;
		case CALIBRATION_MENU_CAL_PAGE_FREQUENCY_VHF:
			firstline = CALIBRATION_MENU_REF_OSC_VHF;
			lastline = CALIBRATION_MENU_REF_OSC_VHF;
			numOptionsOnCurrentPage = 1;
			menuNumberOffset = 3;
			focusNumberOffset = 3;
			displayPrintCore(0, DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT + (-1 * MENU_ENTRY_HEIGHT), "145MHz", FONT_SIZE_3, TEXT_ALIGN_CENTER, false);
			break;
		case CALIBRATION_MENU_CAL_PAGE_FREQUENCY_UHF:
			firstline = CALIBRATION_MENU_REF_OSC_UHF;
			lastline = CALIBRATION_MENU_REF_OSC_UHF;
			numOptionsOnCurrentPage = 1;
			menuNumberOffset = 4;
			focusNumberOffset = 4;
			displayPrintCore(0, DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT + (-1 * MENU_ENTRY_HEIGHT), "435MHz", FONT_SIZE_3, TEXT_ALIGN_CENTER, false);
			break;
		case CALIBRATION_MENU_PAGE_FACTORY_RESET:
			firstline = CALIBRATION_MENU_FACTORY;
			lastline = CALIBRATION_MENU_FACTORY;
			numOptionsOnCurrentPage = 1;
			menuNumberOffset = 5;
			focusNumberOffset = 5;
			break;
		default:// to please the compiler
			break;
	}

	for(int i = firstline; i <= lastline; i++)
	{
		if ((settingOption == false) || (i == 0))
		{
			mNum = i;
			buf[0] = 0;
			buf[2] = 0;
			leftSide = NULL;
			rightSideConst = NULL;
			rightSideVar[0] = 0;
			rightSideUnitsPrompt = PROMPT_SILENCE;// use PROMPT_SILENCE as flag that the unit has not been set
			rightSideUnitsStr = NULL;

			switch(mNum)
			{
				case CALIBRATION_MENU_CAL_FREQUENCY:// Calibration Frequency (from cal table)
					leftSide = currentLanguage->cal_frequency;
					uint32_t val_before_dp = calFreq[freqIndex] / 100000;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%u ", val_before_dp);
					rightSideUnitsPrompt = PROMPT_MEGAHERTZ;
					rightSideUnitsStr = "MHz";
					break;
				case CALIBRATION_MENU_POWER_LEVEL:// Power Level
					leftSide = currentLanguage->cal_pwr;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d", getCalPower(powerIndex));
					rightSideUnitsPrompt = PROMPT_WATTS ;
					rightSideUnitsStr = "W";
					break;
				case CALIBRATION_MENU_POWER_SET:// Power Setting
					leftSide = currentLanguage->pwr_set;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d", powerSetting[freqIndex][powerIndex]);
					break;
				case CALIBRATION_MENU_REF_OSC_VHF://  Reference Oscillator Tuning
					leftSide = currentLanguage->freq_set_VHF;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d", oscTune[0]);
					break;
				case CALIBRATION_MENU_REF_OSC_UHF://  Reference Oscillator Tuning
					leftSide = currentLanguage->freq_set_UHF;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d", oscTune[1]);
					break;
				case CALIBRATION_MENU_FACTORY:// Factory Reset
					leftSide = currentLanguage->factory_reset;
					break;
			}

			const char *rightSide = (rightSideVar[0] ? rightSideVar : (rightSideConst ? rightSideConst : ""));
			snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s%s%s", leftSide, rightSide[0] ? ":" : "", rightSide );

			int voicetrig = menuDataGlobal.currentItemIndex;


			if ((i - focusNumberOffset) == voicetrig)
			{
				bool wasPlaying = voicePromptsIsPlaying();

				if (!isFirstRun && (menuDataGlobal.menuOptionsSetQuickkey == 0))
				{
					voicePromptsInit();
				}

				if (!wasPlaying || (menuDataGlobal.newOptionSelected || (menuDataGlobal.menuOptionsTimeout > 0)))
				{
					voicePromptsAppendLanguageString(leftSide);
				}

				if ((rightSideVar[0] != 0) || ((rightSideVar[0] == 0) && (rightSideConst == NULL)))
				{
					voicePromptsAppendString(rightSideVar);
				}
				else
				{
					voicePromptsAppendLanguageString(rightSideConst);
				}

				if (rightSideUnitsPrompt != PROMPT_SILENCE)
				{
					voicePromptsAppendPrompt(rightSideUnitsPrompt);
				}

				if (rightSideUnitsStr != NULL)
				{
					strncat(rightSideVar, rightSideUnitsStr, SCREEN_LINE_BUFFER_SIZE);
				}

				if (menuDataGlobal.menuOptionsTimeout != -1)
				{
					promptsPlayNotAfterTx();
				}
				else
				{
					menuDataGlobal.menuOptionsTimeout = 0;// clear flag indicating that a QuickKey has just been set
				}
			}

			// QuickKeys
			if (menuDataGlobal.menuOptionsTimeout > 0)
			{
				menuDisplaySettingOption(leftSide, (rightSideVar[0] ? rightSideVar : rightSideConst));
			}
			else
			{
				if (rightSideUnitsStr != NULL)
				{
					strncat(buf, rightSideUnitsStr, SCREEN_LINE_BUFFER_SIZE);
				}

				menuDisplayEntry((i - menuNumberOffset), (mNum - focusNumberOffset), buf, (strlen(leftSide) + 1), THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
			}
		}
	}

	displayRender();
}

static void handleEvent(uiEvent_t *ev)
{
	bool isDirty = false;
	static uint8_t promptSave;

	//if PTT pressed (special case while in the calibration menu)
	if (GPIO_PinRead(GPIO_PTT, Pin_PTT) == 0)
	{
		if(!calIsTransmitting)
		{
			uint32_t freq;
			int oscTuneIndex;
			rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
			promptSave = nonVolatileSettings.audioPromptMode;
			nonVolatileSettings.audioPromptMode = AUDIO_PROMPT_MODE_SILENT;			    //can't have prompts when adjusting because it interferes when changing the values
			trxSetModeAndBandwidth(RADIO_MODE_ANALOG, true);
			switch(pageNumber)
			{
				case CALIBRATION_MENU_CAL_PAGE_FREQUENCY_VHF:
					freq = 14500000;
					oscTuneIndex = 0;
					break;
				case CALIBRATION_MENU_CAL_PAGE_FREQUENCY_UHF:
					freq = 43500000;
					oscTuneIndex = 1;
					break;
				default:
					freq = calFreq[freqIndex];
					oscTuneIndex = freqIndex > 7 ? 1 : 0;
					break;
			}

			trxSetFrequency(freq, freq, DMR_MODE_AUTO);
			trxEnableTransmission();
			setDACTemp(freqIndex, powerIndex);		  //Temporarily use the current power setting
			setRefOscTemp(oscTune[oscTuneIndex]);           //temporarily use this value for Osc Tuning
			calIsTransmitting = true;
		}
	}
	else										//PTT not pressed
	{
		if(calIsTransmitting)
		{
			terminateTransmission();
			calIsTransmitting = false;
			nonVolatileSettings.audioPromptMode = promptSave; 			//restore prompt mode
		}
	}

	if (ev->events & BUTTON_EVENT)
	{
		if (repeatVoicePromptOnSK1(ev))
		{
			return;
		}
	}

	if ((menuDataGlobal.menuOptionsTimeout > 0) && (!BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		if (voicePromptsIsPlaying() == false)
		{
			menuDataGlobal.menuOptionsTimeout--;
			if (menuDataGlobal.menuOptionsTimeout == 0)
			{
				applySettings(false);
				menuSystemPopPreviousMenu();
				return;
			}
		}
	}

	if (ev->events & FUNCTION_EVENT)
	{
		isDirty = true;
		if (ev->function == FUNC_REDRAW)
		{
			updateScreen(false);
			return;
		}
		else if ((QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU) && (QUICKKEY_ENTRYID(ev->function) < NUM_CALIBRATION_MENU_ITEMS))
		{
			menuDataGlobal.currentItemIndex = QUICKKEY_ENTRYID(ev->function);
		}

		if ((QUICKKEY_FUNCTIONID(ev->function) != 0))
		{
			menuDataGlobal.menuOptionsTimeout = 1000;
		}
	}

	if ((ev->events & KEY_EVENT) && (menuDataGlobal.menuOptionsSetQuickkey == 0) && (menuDataGlobal.menuOptionsTimeout == 0))
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
		{
			if (!BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				if (menuDataGlobal.currentItemIndex < (numOptionsOnCurrentPage-1))
				{
					isDirty = true;
					menuDataGlobal.currentItemIndex++;
					menuDataGlobal.newOptionSelected = true;
					menuOptionsExitCode |= MENU_STATUS_LIST_TYPE;
				}
			}
			else
			{
				if (pageNumber < (NUM_CALIBRATION_MENU_PAGES-1))
				{
					menuDataGlobal.currentItemIndex = 0;
					menuDataGlobal.newOptionSelected = true;
					pageNumber++;
					isDirty = true;
				}
			}
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			if (!BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				if (menuDataGlobal.currentItemIndex > 0)
				{
					isDirty = true;
					menuDataGlobal.currentItemIndex--;
					menuDataGlobal.newOptionSelected = true;
					menuOptionsExitCode |= MENU_STATUS_LIST_TYPE;
				}
			}
			else
			{
				if (pageNumber > 0)
				{
					menuDataGlobal.currentItemIndex = 0;
					menuDataGlobal.newOptionSelected = true;
					pageNumber--;
					isDirty = true;
				}
			}
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			applySettings((BUTTONCHECK_DOWN(ev, BUTTON_SK2) != 0));
			menuSystemPopAllAndDisplayRootMenu();
			return;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
			return;
		}
#if defined(HAS_DUMPS_CALIBRATION_TABLE)
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			dumpCalibrationTable();
		}
#endif
		else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
		{
			menuDataGlobal.menuOptionsSetQuickkey = ev->keys.key;
			isDirty = true;
		}
	}
	if ((ev->events & (KEY_EVENT | FUNCTION_EVENT)) && (menuDataGlobal.menuOptionsSetQuickkey == 0))
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_RIGHT) || (QUICKKEY_FUNCTIONID(ev->function) == FUNC_RIGHT))
		{
			int freqBand = 0;
			isDirty = true;
			menuDataGlobal.newOptionSelected = false;
			switch(menuDataGlobal.currentItemIndex + PAGE_ITEMS_OFFSET[pageNumber])
			{
				case CALIBRATION_MENU_REF_OSC_UHF:
					freqBand = 1;
					// fall through
				case CALIBRATION_MENU_REF_OSC_VHF:
					if (oscTune[freqBand] < 256)
					{
						oscTune[freqBand]++;
						setRefOscTemp(oscTune[freqBand]);			//temporarily use this value for Osc Tuning
					}
					break;
				case CALIBRATION_MENU_CAL_FREQUENCY:
					if (freqIndex < NO_OF_CAL_FREQS -1)
					{
						freqIndex++;
						terminateTransmission();
						trxSetFrequency(calFreq[freqIndex], calFreq[freqIndex], DMR_MODE_AUTO);
						if(calIsTransmitting)
						{
							trxEnableTransmission();
							setDACTemp(freqIndex, powerIndex);		  //Temporarily use the current power setting
							setRefOscTemp(oscTune[freqIndex > 7 ? 1 : 0]);           //temporarily use this value for Osc Tuning
						}
					}
					break;
				case CALIBRATION_MENU_POWER_LEVEL:
					if (powerIndex < NO_OF_POWER_LEVELS -1)
					{
						powerIndex++;
						setDACTemp(freqIndex, powerIndex);		  //Temporarily use the current power setting
					}
					break;
				case CALIBRATION_MENU_POWER_SET:
					if (powerSetting[freqIndex][powerIndex] < 255)
					{
						powerSetting[freqIndex][powerIndex]++;
						setDACTemp(freqIndex, powerIndex);		  //Temporarily use the current power setting
					}
					break;
			}
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_LEFT) || (QUICKKEY_FUNCTIONID(ev->function) == FUNC_LEFT))
		{
			int refOscFreqBand = 0;
			isDirty = true;
			menuDataGlobal.newOptionSelected = false;
			switch(menuDataGlobal.currentItemIndex + PAGE_ITEMS_OFFSET[pageNumber])
			{
				case CALIBRATION_MENU_REF_OSC_UHF:
					refOscFreqBand = 1;
					// fall through
				case CALIBRATION_MENU_REF_OSC_VHF:
					if (oscTune[refOscFreqBand] > -256)
					{
						oscTune[refOscFreqBand]--;
						setRefOscTemp(oscTune[refOscFreqBand]);           //temporarily use this value for Osc Tuning
					}
					break;
				case CALIBRATION_MENU_CAL_FREQUENCY:
					if (freqIndex > 0)
					{
						freqIndex--;
						terminateTransmission();
						trxSetFrequency(calFreq[freqIndex], calFreq[freqIndex], DMR_MODE_AUTO);
						if(calIsTransmitting)
						{
							trxEnableTransmission();
							setDACTemp(freqIndex, powerIndex);		  //Temporarily use the current power setting
							setRefOscTemp(oscTune[freqIndex > 7 ? 1 : 0]);           //temporarily use this value for Osc Tuning
						}
					}
					break;
				case CALIBRATION_MENU_POWER_LEVEL:
					if (powerIndex > 0)
					{
						powerIndex--;
						setDACTemp(freqIndex, powerIndex);		  //Temporarily use the current power setting
					}
					break;
				case CALIBRATION_MENU_POWER_SET:
					if (powerSetting[freqIndex][powerIndex] > 0)
					{
						powerSetting[freqIndex][powerIndex]--;
						setDACTemp(freqIndex, powerIndex);		  //Temporarily use the current power setting
					}
					break;
			}
		}
		else if ((ev->keys.event & KEY_MOD_PRESS) && (menuDataGlobal.menuOptionsTimeout > 0))
		{
			menuDataGlobal.menuOptionsTimeout = 0;;
			terminateTransmission();
			menuSystemPopPreviousMenu();
			return;
		}
	}

	if (uiQuickKeysIsStoring(ev))
	{
		uiQuickKeysStore(ev, &menuOptionsExitCode);
		isDirty = true;
	}

	if (isDirty)
	{
		updateScreen(false);
	}
}

//Temporarily adjust the Ref Oscillator value
static void setRefOscTemp(int16_t refOscOffset)
{
	if (refOscOffset < 0)
	{
		refOscOffset += 1024;
	}

	SPI0WritePageRegByte(0x04, 0x48, (refOscOffset >> 8) & 0x03); // bit 0 to 1 = upper 2 bits of 10-bit twopoint mod
	SPI0WritePageRegByte(0x04, 0x47, (refOscOffset & 0xFF)); // bit 0 to 7 = lower 8 bits of 10-bit twopoint mod
	SPI0WritePageRegByte(0x04, 0x04, (refOscOffset & 0xFF)); // bit 0 to 7 = lower 8 bits of 10-bit twopoint mod
}

//Temporarily adjust the Tx Power DAC
static void setDACTemp(uint8_t freqIndex, uint8_t powerIndex)
{
	DAC_SetBufferValue(DAC0, 0U, ((uint32_t)powerSetting[freqIndex][powerIndex]) << 4);	// PA drive to appropriate level
}

static void terminateTransmission(void)
{
	trxDisableTransmission();
	trxTransmissionEnabled = false;
}

static void applySettings(bool sk2Down)
{
	//copy the new calibration values to calibration table
	calibrationPutVHFOscTune(oscTune[0]);
	calibrationPutUHFOscTune(oscTune[1]);
	for(int i = 0 ; i < NO_OF_CAL_FREQS ; i++)
	{
		for(int j = 0 ; j < NO_OF_POWER_LEVELS ; j++)
		{
			calibrationPutPower(i , j , powerSetting[i][j] );
		}
	}

	if(sk2Down)					//only save to flash or factory reset if SK2 + green is pressed.
	{
		if (pageNumber == CALIBRATION_MENU_PAGE_FACTORY_RESET)
		{
			calibrationCheckAndCopyToCommonLocation(true);
			pageNumber = CALIBRATION_MENU_PAGE_POWER;
			menuDataGlobal.currentItemIndex = 0;
			menuDataGlobal.newOptionSelected = true;
		}
		else
		{
			calibrationSaveLocal();
		}
	}

	trxConfigurePA_DAC_ForFrequencyBand(true);
}

static void exitCallback(void *data)
{
	trxSetFrequency(currentChannelData->rxFreq + 10, currentChannelData->txFreq + 10, DMR_MODE_AUTO);      //FORCE A SMALL FREQUENCY CHANGE TO TIDY UP
	trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
	trxSetModeAndBandwidth(currentChannelData->chMode, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));
	terminateTransmission();
}
