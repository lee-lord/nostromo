#pragma once

#define EEPROM_CONF_VERSION 1

bool eepromValid(void);
void eepromRead(void);
void eepromWrite(void);
