/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <platform.h>

#include "build_config.h"

#include "gpio.h"
#include "system.h"
#include "drivers/io_impl.h"

#include "bus_i2c.h"

#ifndef SOFT_I2C

#define I2C_SHORT_TIMEOUT   ((uint32_t)0x1000)
#define I2C_LONG_TIMEOUT    ((uint32_t)(10 * I2C_SHORT_TIMEOUT))
#define I2C_GPIO_AF         GPIO_AF_4 

#ifndef I2C1_SCL 
#define I2C1_SCL PB6 
#endif
#ifndef I2C1_SDA 
#define I2C1_SDA PB7 
#endif
#ifndef I2C2_SCL 
#define I2C2_SCL PF4 
#endif
#ifndef I2C2_SDA 
#define I2C2_SDA PA10
#endif

static uint32_t i2cTimeout;

static volatile uint16_t i2cErrorCount = 0;
//static volatile uint16_t i2c2ErrorCount = 0;

static i2cDevice_t i2cHardwareMap[] = {
	{ .dev = I2C1, .scl = IO_TAG(I2C1_SCL), .sda = IO_TAG(I2C1_SDA), .rcc = RCC_APB1(I2C1), .overClock = false },
	{ .dev = I2C2, .scl = IO_TAG(I2C2_SCL), .sda = IO_TAG(I2C2_SDA), .rcc = RCC_APB1(I2C2), .overClock = false }
};

///////////////////////////////////////////////////////////////////////////////
// I2C TimeoutUserCallback
///////////////////////////////////////////////////////////////////////////////

uint32_t i2cTimeoutUserCallback(void)
{
	i2cErrorCount++;
	return false;
}

static I2CDevice I2Cx_device;

void i2cInit(I2CDevice device)
{
    I2Cx_device = device;
    
	i2cDevice_t *i2c;
	i2c = &(i2cHardwareMap[device]);

	I2C_TypeDef *I2Cx;
	I2Cx = i2c->dev;
       
	IO_t scl = IOGetByTag(i2c->scl);
	IO_t sda = IOGetByTag(i2c->sda);
    
	RCC_ClockCmd(i2c->rcc, ENABLE);
	RCC_I2CCLKConfig(I2Cx == I2C2 ? RCC_I2C2CLK_SYSCLK : RCC_I2C1CLK_SYSCLK);

	IOConfigGPIOAF(scl, IO_CONFIG(GPIO_Mode_AF, GPIO_Speed_50MHz, GPIO_OType_OD, GPIO_PuPd_NOPULL), GPIO_AF_4);
	IOConfigGPIOAF(sda, IO_CONFIG(GPIO_Mode_AF, GPIO_Speed_50MHz, GPIO_OType_OD, GPIO_PuPd_NOPULL), GPIO_AF_4);

	I2C_InitTypeDef i2cInit = {
		.I2C_Mode = I2C_Mode_I2C,
		.I2C_AnalogFilter = I2C_AnalogFilter_Enable,
		.I2C_DigitalFilter = 0x00,
		.I2C_OwnAddress1 = 0x00,
		.I2C_Ack = I2C_Ack_Enable,
		.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit,
		.I2C_Timing = 0x00E0257A, // 400 Khz, 72Mhz Clock, Analog Filter Delay ON, Rise 100, Fall 10.
		//.I2C_Timing              = 0x8000050B;
	};
    
	if (i2c->overClock) {
		i2cInit.I2C_Timing = 0x00500E30; // 1000 Khz, 72Mhz Clock, Analog Filter Delay ON, Setup 40, Hold 4.
	}
	I2C_Init(I2Cx, &i2cInit);

	I2C_Cmd(I2Cx, ENABLE);
}

uint16_t i2cGetErrorCounter(void)
{
	return i2cErrorCount;
}

bool i2cWriteByDevice(I2CDevice device, uint8_t addr_, uint8_t reg, uint8_t data)
{
	addr_ <<= 1;

	I2C_TypeDef *I2Cx;
	I2Cx = i2cHardwareMap[device].dev; 
    
	/* Test on BUSY Flag */
	i2cTimeout = I2C_LONG_TIMEOUT;
	while (I2C_GetFlagStatus(I2Cx, I2C_ISR_BUSY) != RESET) {
		if ((i2cTimeout--) == 0) {
			return i2cTimeoutUserCallback();
		}
	}

	    /* Configure slave address, nbytes, reload, end mode and start or stop generation */
	I2C_TransferHandling(I2Cx, addr_, 1, I2C_Reload_Mode, I2C_Generate_Start_Write);

	    /* Wait until TXIS flag is set */
	i2cTimeout = I2C_LONG_TIMEOUT;
	while (I2C_GetFlagStatus(I2Cx, I2C_ISR_TXIS) == RESET) {
		if ((i2cTimeout--) == 0) {
			return i2cTimeoutUserCallback();
		}
	}

	    /* Send Register address */
	I2C_SendData(I2Cx, (uint8_t) reg);

	    /* Wait until TCR flag is set */
	i2cTimeout = I2C_LONG_TIMEOUT;
	while (I2C_GetFlagStatus(I2Cx, I2C_ISR_TCR) == RESET)
	{
		if ((i2cTimeout--) == 0) {
			return i2cTimeoutUserCallback();
		}
	}

	    /* Configure slave address, nbytes, reload, end mode and start or stop generation */
	I2C_TransferHandling(I2Cx, addr_, 1, I2C_AutoEnd_Mode, I2C_No_StartStop);

	    /* Wait until TXIS flag is set */
	i2cTimeout = I2C_LONG_TIMEOUT;
	while (I2C_GetFlagStatus(I2Cx, I2C_ISR_TXIS) == RESET) {
		if ((i2cTimeout--) == 0) {
			return i2cTimeoutUserCallback();
		}
	}

	    /* Write data to TXDR */
	I2C_SendData(I2Cx, data);

	    /* Wait until STOPF flag is set */
	i2cTimeout = I2C_LONG_TIMEOUT;
	while (I2C_GetFlagStatus(I2Cx, I2C_ISR_STOPF) == RESET) {
		if ((i2cTimeout--) == 0) {
			return i2cTimeoutUserCallback();
		}
	}

	    /* Clear STOPF flag */
	I2C_ClearFlag(I2Cx, I2C_ICR_STOPCF);

	return true;
}

bool i2cReadByDevice(I2CDevice device, uint8_t addr_, uint8_t reg, uint8_t len, uint8_t* buf)
{
	addr_ <<= 1;

	I2C_TypeDef *I2Cx;
	I2Cx = i2cHardwareMap[device].dev; 
        
	/* Test on BUSY Flag */
	i2cTimeout = I2C_LONG_TIMEOUT;
	while (I2C_GetFlagStatus(I2Cx, I2C_ISR_BUSY) != RESET) {
		if ((i2cTimeout--) == 0) {
			return i2cTimeoutUserCallback();
		}
	}

	    /* Configure slave address, nbytes, reload, end mode and start or stop generation */
	I2C_TransferHandling(I2Cx, addr_, 1, I2C_SoftEnd_Mode, I2C_Generate_Start_Write);

	    /* Wait until TXIS flag is set */
	i2cTimeout = I2C_LONG_TIMEOUT;
	while (I2C_GetFlagStatus(I2Cx, I2C_ISR_TXIS) == RESET) {
		if ((i2cTimeout--) == 0) {
			return i2cTimeoutUserCallback();
		}
	}

	    /* Send Register address */
	I2C_SendData(I2Cx, (uint8_t) reg);

	    /* Wait until TC flag is set */
	i2cTimeout = I2C_LONG_TIMEOUT;
	while (I2C_GetFlagStatus(I2Cx, I2C_ISR_TC) == RESET) {
		if ((i2cTimeout--) == 0) {
			return i2cTimeoutUserCallback();
		}
	}

	    /* Configure slave address, nbytes, reload, end mode and start or stop generation */
	I2C_TransferHandling(I2Cx, addr_, len, I2C_AutoEnd_Mode, I2C_Generate_Start_Read);

	    /* Wait until all data are received */
	while (len) {
	    /* Wait until RXNE flag is set */
		i2cTimeout = I2C_LONG_TIMEOUT;
		while (I2C_GetFlagStatus(I2Cx, I2C_ISR_RXNE) == RESET) {
			if ((i2cTimeout--) == 0) {
				return i2cTimeoutUserCallback();
			}
		}

		        /* Read data from RXDR */
		*buf = I2C_ReceiveData(I2Cx);
		/* Point to the next location where the byte read will be saved */
		buf++;

		        /* Decrement the read bytes counter */
		len--;
	}

	    /* Wait until STOPF flag is set */
	i2cTimeout = I2C_LONG_TIMEOUT;
	while (I2C_GetFlagStatus(I2Cx, I2C_ISR_STOPF) == RESET) {
		if ((i2cTimeout--) == 0) {
			return i2cTimeoutUserCallback();
		}
	}

	    /* Clear STOPF flag */
	I2C_ClearFlag(I2Cx, I2C_ICR_STOPCF);

	    /* If all operations OK */
	return true;
}

bool i2cWriteBuffer(uint8_t addr_, uint8_t reg_, uint8_t len_, uint8_t *data)
{
    return i2cWriteBufferByDevice(I2Cx_device, addr_, reg_, len_, data);
}

bool i2cWrite(uint8_t addr_, uint8_t reg, uint8_t data)
{
    return i2cWriteByDevice(I2Cx_device, addr_, reg, data);
}

bool i2cRead(uint8_t addr_, uint8_t reg, uint8_t len, uint8_t* buf)
{
    return i2cReadByDevice(I2Cx_device, addr_, reg, len, buf);    
}

#endif
