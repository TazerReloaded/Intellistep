#include "encoder.h"

// SPI init structure
SPI_HandleTypeDef spiConfig;

// Main initialization structure
GPIO_InitTypeDef GPIO_InitStructure;

// A storage for the last angle sampled
#ifdef ENCODER_SPEED_ESTIMATION
    double lastEncoderAngle = 0;
    uint32_t lastAngleSampleTime = 0;
#endif

// Moving average instances
MovingAverage <float> encoderSpeedAvg;
MovingAverage <float> encoderAngleAvg;
MovingAverage <float> encoderAbsoluteAngleAvg;
MovingAverage <float> encoderTempAvg;

// The startup angle and rev offsets
double startupAngleOffset = 0;
double startupAngleRevOffset = 0;

// A map of the known registers
uint16_t regMap[MAX_NUM_REG];              //!< Register map */

// Massive bit field table
const BitField_t bitFields[] = {
	{REG_ACCESS_RU,  REG_STAT,    0x2,    1,  0x00,  0},       //!< 00 bits 0:0 SRST status watch dog
	{REG_ACCESS_R,   REG_STAT,    0x2,    1,  0x00,  0},       //!< 01 bits 1:1 SWD status watch dog
	{REG_ACCESS_R,   REG_STAT,    0x4,    2,  0x00,  0},       //!< 02 bits 2:2 SVR status voltage regulator
	{REG_ACCESS_R,   REG_STAT,    0x8,    3,  0x00,  0},       //!< 03 bits 3:3 SFUSE status fuses
	{REG_ACCESS_R,   REG_STAT,    0x10,   4,  0x00,  0},       //!< 04 bits 4:4 SDSPU status digital signal processing unit
	{REG_ACCESS_RU,  REG_STAT,    0x20,   5,  0x00,  0},       //!< 05 bits 5:5 SOV status overflow
	{REG_ACCESS_RU,  REG_STAT,    0x40,   6,  0x00,  0},       //!< 06 bits 6:6 SXYOL status X/Y data out limit
	{REG_ACCESS_RU,  REG_STAT,    0x80,   7,  0x00,  0},       //!< 07 bits 7:7 SMAGOL status magnitude out limit
	{REG_ACCESS_RES, REG_STAT,    0x100,  8,  0x00,  0},       //!< 08 bits 8:8 reserved
	{REG_ACCESS_R,   REG_STAT,    0x200,  9,  0x00,  0},       //!< 09 bits 9:9 SADCT status ADC test
	{REG_ACCESS_R,   REG_STAT,    0x400,  10, 0x00,  0},       //!< 10 bits 10:10 SROM status ROM
	{REG_ACCESS_RU,  REG_STAT,    0x800,  11, 0x00,  0},       //!< 11 bits 11:11 NOGMRXY no valid GMR XY Values
	{REG_ACCESS_RU,  REG_STAT,    0x1000, 12, 0x00,  0},       //!< 12 bits 12:12 NOGMRA no valid GMR Angle Value
	{REG_ACCESS_RW,  REG_STAT,    0x6000, 13, 0x00,  0},       //!< 13 bits 14:13 SNR slave number
	{REG_ACCESS_RU,  REG_STAT,    0x8000, 15, 0x00,  0},       //!< 14 bits 15:15 RDST read status

	{REG_ACCESS_RW,  REG_ACSTAT,  0x1,    0,  0x00,  1},       //!< 15 bits 0:0 ASRST Activation of Hardware Reset
	{REG_ACCESS_RWU, REG_ACSTAT,  0x2,    1,  0x00,  1},       //!< 16 bits 1:1 ASWD Enable DSPU Watch dog
	{REG_ACCESS_RWU, REG_ACSTAT,  0x4,    2,  0x00,  1},       //!< 17 bits 2:2 ASVR Enable Voltage regulator Check
	{REG_ACCESS_RWU, REG_ACSTAT,  0x8,    3,  0x00,  1},       //!< 18 bits 3:3 ASFUSE Activation Fuse CRC
	{REG_ACCESS_RWU, REG_ACSTAT,  0x10,   4,  0x00,  1},       //!< 19 bits 4:4 ASDSPU Activation DSPU BIST
	{REG_ACCESS_RWU, REG_ACSTAT,  0x20,   5,  0x00,  1},       //!< 20 bits 5:5 ASOV Enable of DSPU Overflow Check
	{REG_ACCESS_RWU, REG_ACSTAT,  0x40,   6,  0x00,  1},       //!< 21 bits 6:6 ASVECXY Activation of X,Y Out of Limit-Check
	{REG_ACCESS_RWU, REG_ACSTAT,  0x80,   7,  0x00,  1},       //!< 22 bits 7:7 ASVEGMAG Activation of Magnitude Check
	{REG_ACCESS_RES, REG_ACSTAT,  0x100,  8,  0x00,  1},       //!< 23 bits 8:8 Reserved
	{REG_ACCESS_RWU, REG_ACSTAT,  0x200,  9,  0x00,  1},       //!< 24 bits 9:9 ASADCT Enable ADC Test vector Check
	{REG_ACCESS_RWU, REG_ACSTAT,  0x400,  10, 0x00,  1},       //!< 25 bits 10:10 ASFRST Activation of Firmware Reset
	{REG_ACCESS_RES, REG_ACSTAT,  0xF800, 11, 0x00,  1},       //!< 26 bits 15:11 Reserved

	{REG_ACCESS_RU,  REG_AVAL,    0x7FFF, 0,  0x00,  2},       //!< 27 bits 14:0 ANGVAL Calculated Angle Value (signed 15-bit)
	{REG_ACCESS_R,   REG_AVAL,    0x8000, 15, 0x00,  2},       //!< 28 bits 15:15 RDAV Read Status, Angle Value

	{REG_ACCESS_RU,  REG_ASPD,    0x7FFF, 0,  0x00,  3},       //!< 29 bits 14:0 ANGSPD Signed value, where the sign bit [14] indicates the direction of the rotation
	{REG_ACCESS_R,   REG_ASPD,    0x8000, 15, 0x00,  3},       //!< 30 bits 15:15 RDAS Read Status, Angle Speed

	{REG_ACCESS_RU,  REG_AREV,    0xFF,   0,  0x00,  4},       //!< 31 bits 8:0 REVOL Revolution counter. Increments for every full rotation in counter-clockwise direction
	{REG_ACCESS_RWU, REG_AREV,    0x7E00, 9,  0x00,  4},       //!< 32 bits 14:9 FCNT Internal frame counter. Increments every update period
	{REG_ACCESS_R,   REG_AREV,    0x8000, 15, 0x00,  4},       //!< 33 its 15:15 RDREV Read Status, Revolution

	{REG_ACCESS_RWU, REG_FSYNC,   0xFF,   0,  0x00,  5},       //!< 34 bits 8:0 TEMPR Signed offset compensated temperature value
	{REG_ACCESS_RU,  REG_FSYNC,   0xFE00, 9,  0x00,  5},       //!< 35 bits 15:9 FSYNC Frame Synchronization Counter Value

	{REG_ACCESS_RW,  REG_MOD_1,   0x3,    0,  0x00,  6},       //!< 36 bits 1:0 IIFMOD Incremental Interface Mode
	{REG_ACCESS_RW,  REG_MOD_1,   0x4,    2,  0x00,  6},       //!< 37 bits 2:2 DSPUHOLD if DSPU is on hold, no watch dog reset is performed by DSPU
	{REG_ACCESS_RES, REG_MOD_1,   0x8,    3,  0x00,  6},       //!< 38 bits 3:3 Reserved1
	{REG_ACCESS_RW,  REG_MOD_1,   0x10,   4,  0x00,  6},       //!< 39 bits 4:4 CLKSEL switch to external clock at start-up only
	{REG_ACCESS_RES, REG_MOD_1,   0x3FE0, 5,  0x00,  6},       //!< 40 bits 13:5 Reserved2
	{REG_ACCESS_RW,  REG_MOD_1,   0x6000, 13, 0x00,  6},       //!< 41 bits 15:14 FIRMD Update Rate Setting

	{REG_ACCESS_RW,  REG_SIL,     0x7,    0,  0x00,  7},       //!< 42 bits 2:0 ADCTVX Test vector X
	{REG_ACCESS_RW,  REG_SIL,     0x38,   3,  0x00,  7},       //!< 43 bits 5:3 ADCTVY Test vector Y
	{REG_ACCESS_RW,  REG_SIL,     0x40,   6,  0x00,  7},       //!< 44 bits 6:6 ADCTVEN Sensor elements are internally disconnected and test voltages are connected to ADCs
	{REG_ACCESS_RES, REG_SIL,     0x380,  7,  0x00,  7},       //!< 45 bits 9:7 Reserved1
	{REG_ACCESS_RW,  REG_SIL,     0x400,  10, 0x00,  7},       //!< 46 bits 10:10 FUSEREL Triggers reload of default values from laser fuses into configuration registers
	{REG_ACCESS_RES, REG_SIL,     0x3800, 11, 0x00,  7},       //!< 47 bits 13:11 Reserved2
	{REG_ACCESS_RW,  REG_SIL,     0x4000, 14, 0x00,  7},       //!< 48 bits 14:14 FILTINV the X- and Y-signals are inverted. The angle output is then shifted by 180°
	{REG_ACCESS_RW,  REG_SIL,     0x8000, 15, 0x00,  7},       //!< 49 bits 15:15 FILTPAR the raw X-signal is routed also to the raw Y-signal input of the filter so SIN and COS signal should be identical

	{REG_ACCESS_RW,  REG_MOD_2,   0x3,    0,  0x00,  8},       //!< 50 bits 1:0 AUTOCAL Automatic calibration of offset and amplitude synchronicity for applications with full-turn
	{REG_ACCESS_RW,  REG_MOD_2,   0x4,    2,  0x00,  8},       //!< 51 bits 2:2 PREDICT Prediction of angle value based on current angle speed
	{REG_ACCESS_RW,  REG_MOD_2,   0x8,    3,  0x00,  8},       //!< 52 bits 3:3 ANGDIR Inverts angle and angle speed values and revolution counter behavior
	{REG_ACCESS_RW,  REG_MOD_2,   0x7FF0, 4,  0x00,  8},       //!< 53 bits 14:4 ANGRANGE Changes the representation of the angle output by multiplying the output with a factor ANG_RANGE/128
	{REG_ACCESS_RES, REG_MOD_2,   0x8000, 15, 0x00,  8},       //!< 54 bits 15:15 Reserved1

	{REG_ACCESS_RW,  REG_MOD_3,   0x3,    0,  0x00,  9},       //!< 55 bits 1:0 PADDRV Configuration of Pad-Driver
	{REG_ACCESS_RW,  REG_MOD_3,   0x4,    2,  0x00,  9},       //!< 56 bits 2:2 SSCOD SSC-Interface Data Pin Output Mode
	{REG_ACCESS_RW,  REG_MOD_3,   0x8,    3,  0x00,  9},       //!< 57 bits 3:3 SPIKEF Filters voltage spikes on input pads (IFC, SCK and CSQ)
	{REG_ACCESS_RW,  REG_MOD_3,   0xFFF0, 4,  0x00,  9},       //!< 58 bits 15:4 ANG_BASE Sets the 0° angle position (12 bit value). Angle base is factory-calibrated to make the 0° direction parallel to the edge of the chip

	{REG_ACCESS_RES, REG_OFFX,    0xF,    0,  0x00, 10},       //!< 59 bits 3:0 Reserved1
	{REG_ACCESS_RW,  REG_OFFX,    0xFFF0, 4,  0x00, 10},       //!< 60 bits 15:4 XOFFSET 12-bit signed integer value of raw X-signal offset correction at 25°C

	{REG_ACCESS_RES, REG_OFFY,    0xF,    0,  0x00, 11},       //!< 61 bits 3:0 Reserved1
	{REG_ACCESS_RW,  REG_OFFY,    0xFFF0, 4,  0x00, 11},       //!< 62 bits 15:4 YOFFSET 12-bit signed integer value of raw Y-signal offset correction at 25°C

	{REG_ACCESS_RES, REG_SYNCH,   0xF,    0,  0x00, 12},       //!< 63 bits 3:0 Reserved1
	{REG_ACCESS_RW,  REG_SYNCH,   0xFFF0, 4,  0x00, 12},       //!< 64 bits 15:4 SYNCH 12-bit signed integer value of amplitude synchronicity

	{REG_ACCESS_RW,  REG_IFAB,    0x3,    0,  0x00, 13},       //!< 65 bits 1:0 IFADHYST Hysteresis (multi-purpose)
	{REG_ACCESS_RW,  REG_IFAB,    0x4,    2,  0x00, 13},       //!< 66 bits 2:2 IFABOD IFA,IFB,IFC Output Mode
	{REG_ACCESS_RW,  REG_IFAB,    0x8,    3,  0x00, 13},       //!< 67 bits 3:3 FIRUDR Initial filter update rate (FIR)
	{REG_ACCESS_RW,  REG_IFAB,    0xFFF0, 4,  0x00, 13},       //!< 68 bits 15:4 ORTHO Orthogonality Correction of X and Y Components

	{REG_ACCESS_RW,  REG_MOD_4,   0x3,    0,  0x00, 14},       //!< 69 bits 1:0 IFMD Interface Mode on IFA,IFB,IFC
	{REG_ACCESS_RES, REG_MOD_4,   0x4,    2,  0x00, 14},       //!< 70 bits 2:2 Reserved1
	{REG_ACCESS_RW,  REG_MOD_4,   0x18,   3,  0x00, 14},       //!< 71 bits 4:3 IFABRES IIF resolution (multi-purpose)
	{REG_ACCESS_RW,  REG_MOD_4,   0x1E0,  5,  0x00, 14},       //!< 72 bits 8:5 HSMPLP Hall Switch mode (multi-purpose)
	{REG_ACCESS_RW,  REG_MOD_4,   0x7E00, 9,  0x00, 14},       //!< 73 bits 15:9 TCOXT 7-bit signed integer value of X-offset temperature coefficient

	{REG_ACCESS_RW,  REG_TCO_Y,   0x7F,   0,  0x00, 15},       //!< 74 bits 7:0 CRCPAR CRC of Parameters
	{REG_ACCESS_RW,  REG_TCO_Y,   0x80,   8,  0x00, 15},       //!< 75 bits 8:8 SBIST Startup-BIST
	{REG_ACCESS_RW,  REG_TCO_Y,   0x7E00, 9,  0x00, 15},       //!< 76 bits 15:9 TCOYT 7-bit signed integer value of Y-offset temperature coefficient

	{REG_ACCESS_R,   REG_ADC_X,   0xFFFF, 0,  0x00, 16},       //!< 77 bits 15:0 ADCX ADC value of X-GMR

	{REG_ACCESS_R,   REG_ADC_Y,   0xFFFF, 0,  0x00, 17},       //!< 78 bits 15:0 ADCY ADC value of Y-GMR

	{REG_ACCESS_RU,  REG_D_MAG,   0x3FF,  0,  0x00, 18},       //!< 79 bits 9:0 MAG Unsigned Angle Vector Magnitude after X, Y error compensation (due to temperature)
	{REG_ACCESS_RES, REG_D_MAG,   0xFC00, 10, 0x00, 18},       //!< 80 bits 15:10 Reserved1

	{REG_ACCESS_RU,  REG_T_RAW,   0x3FF,  0,  0x00, 19},       //!< 81 bits 9:0 TRAW Temperature Sensor Raw-Value at ADC without offset
	{REG_ACCESS_RES, REG_T_RAW,   0xFC00, 10, 0x00, 19},       //!< 82 bits 14:10 Reserved1
	{REG_ACCESS_RU,  REG_T_RAW,   0x8000, 15, 0x00, 19},       //!< 83 bits 15:15 TTGL Temperature Sensor Raw-Value Toggle toggles after every new temperature value

	{REG_ACCESS_RU,  REG_IIF_CNT, 0x7FFF, 0,  0x00, 20},       //!< 84 bits 14:0 IIFCNT 14 bit counter value of IIF increments
	{REG_ACCESS_RES, REG_IIF_CNT, 0x8000, 15, 0x00, 20},       //!< 85 bits 15:14 Reserved1

	{REG_ACCESS_R,   REG_T25O,    0x1FFF, 0,  0x00, 21},       //!< 86 bit 8:0 T250 Signed offset value at 25°C temperature; 1dig=0.36°C
	{REG_ACCESS_RES, REG_T25O,    0xFE00, 9,  0x00, 21},       //!< 87 bits 15:9 Reserved1
};


// Function to setup the encoder
void initEncoder() {

    // Setup pin A5, A6, and A7
    GPIO_InitStructure.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Enable the clock for the SPI bus
    __HAL_RCC_SPI1_CLK_ENABLE();

    // Set the peripheral to be used
    spiConfig.Instance = SPI1;

    // Configure the settings for transactions
    spiConfig.Init.Direction = SPI_DIRECTION_2LINES;
    spiConfig.Init.Mode = SPI_MODE_MASTER;
    spiConfig.Init.DataSize = SPI_DATASIZE_8BIT;
    spiConfig.Init.CLKPolarity = SPI_POLARITY_LOW;
    spiConfig.Init.CLKPhase = SPI_PHASE_2EDGE;
    spiConfig.Init.NSS = SPI_NSS_SOFT;
    spiConfig.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    spiConfig.Init.FirstBit = SPI_FIRSTBIT_MSB;
    spiConfig.Init.CRCPolynomial = 7;

    // Initialize the SPI bus with the parameters we set
    if (HAL_SPI_Init(&spiConfig) != HAL_OK) {
        Serial.println(F("SPI not initialized!"));
    }

    // Set the chip select pin high, disabling the encoder's communication
    pinMode(ENCODER_CS_PIN, OUTPUT);
    digitalWrite(ENCODER_CS_PIN, HIGH);

    // Reset the encoder's firmware
    //writeToEncoderRegister(ENCODER_ACT_STATUS_REG, 0x401);

    // Setup the moving average calculations
    encoderSpeedAvg.begin(RPM_AVG_READINGS);
    encoderAngleAvg.begin(ANGLE_AVG_READINGS);
    encoderAbsoluteAngleAvg.begin(ANGLE_AVG_READINGS);
    encoderTempAvg.begin(TEMP_AVG_READINGS);

    // Populate the average angle reading table
    for (uint8_t index = 0; index < ANGLE_AVG_READINGS; index++) {
        getAngle();
    }

    // Set the offsets
    startupAngleOffset = getAngle();
    startupAngleRevOffset = getAbsoluteRev();

    // Set the correct starting values for the estimation if using estimation
    #ifdef ENCODER_SPEED_ESTIMATION
        lastEncoderAngle = getAbsoluteAngle();
        lastAngleSampleTime = micros();
    #endif
}


// Read the value of a register
uint16_t readEncoderRegister(uint16_t registerAddress) {

    // Pull CS low to select encoder
    digitalWrite(ENCODER_CS_PIN, LOW);

    // Add read bit to address
    registerAddress |= ENCODER_READ_COMMAND + 1;

    // Setup RX and TX buffers
    uint8_t rxbuf[2];
    uint8_t txbuf[2] = { uint8_t(registerAddress >> 8), uint8_t(registerAddress) };

    // Send address we want to read, response seems to be equal to request
    HAL_SPI_TransmitReceive(&spiConfig, txbuf, rxbuf, 2, 100);

    // Set the MOSI pin to open drain
    GPIO_InitStructure.Pin = GPIO_PIN_7;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_OD;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Send 0xFFFF (like BTT code), this returns the wanted value
    txbuf[0] = 0xFF, txbuf[1] = 0xFF;
    HAL_SPI_TransmitReceive(&spiConfig, txbuf, rxbuf, 2, 100);

    // Set MOSI back to Push/Pull
    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Deselect encoder
    digitalWrite(ENCODER_CS_PIN, HIGH);

    // Return value as uint16
    return rxbuf[0] << 8 | rxbuf[1];
}


// Read multiple registers
void readMultipleEncoderRegisters(uint16_t registerAddress, uint16_t* data, uint16_t dataLength) {

    // Pull CS low to select encoder
    digitalWriteFast(ENCODER_CS_PIN, LOW);

    // Setup TX and RX buffers
    registerAddress |= ENCODER_READ_COMMAND + dataLength;
    uint8_t txbuf[dataLength * 2] = { uint8_t(registerAddress >> 8), uint8_t(registerAddress) };
    uint8_t rxbuf[dataLength * 2];

    // Send address we want to read, response seems to be equal to request
    HAL_SPI_TransmitReceive(&spiConfig, txbuf, rxbuf, 2, 100);

    // Set the MOSI pin to open drain
    GPIO_InitStructure.Pin = GPIO_PIN_7;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_OD;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Send 0xFFFF (like BTT code), this returns the wanted value
    // Array length is doubled as we're using 8 bit values instead of 16
    for (uint8_t i = 0; i < dataLength * 2; i++) {
        txbuf[i] = 0xFF;
    }
    HAL_SPI_TransmitReceive(&spiConfig, txbuf, rxbuf, dataLength * 2, 100);
    
    // Write the received data into the array
    for (uint8_t i = 0; i < dataLength; i++) {
        data[i] = rxbuf[i * 2] << 8 | rxbuf[i * 2 + 1];
    }

    // Set MOSI back to Push/Pull
    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Deselect encoder
    digitalWriteFast(ENCODER_CS_PIN, HIGH);
}


// Write a value to a register
// ! Untested
void writeToEncoderRegister(uint16_t registerAddress, uint16_t data) {

    // Pull CS low to select encoder
    digitalWriteFast(ENCODER_CS_PIN, LOW);

    // Setup TX and RX buffers
    registerAddress |= ENCODER_WRITE_COMMAND + 1;
    uint8_t txbuf[2] = { uint8_t(registerAddress >> 8), uint8_t(registerAddress) };
    uint8_t rxbuf[2];

    // Send address we want to write, response seems to be equal to request
    HAL_SPI_TransmitReceive(&spiConfig, txbuf, rxbuf, 2, 10);

    // Set the MOSI pin to open drain
    GPIO_InitStructure.Pin = GPIO_PIN_7;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_OD;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Send the data to be written
    txbuf[0] = uint8_t(data >> 8), txbuf[1] = uint8_t(data);
    HAL_SPI_TransmitReceive(&spiConfig, txbuf, rxbuf, 2, 10);

    // Set MOSI back to Push/Pull
    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Deselect encoder
    digitalWriteFast(ENCODER_CS_PIN, HIGH);
}


// Get a bit field from a register
uint16_t getBitField(BitField_t bitField) {

    // Check to make sure that the value can be read
	if ((REG_ACCESS_R & bitField.regAccess) == REG_ACCESS_R) {
		if ((REG_ACCESS_U & bitField.regAccess) == REG_ACCESS_U) {
			// ! p->sBus->triggerUpdate();
		}

        // Read the encoder register
		regMap[bitField.posMap] = readEncoderRegister(addrFields[bitField.posMap].regAddress);

        // Return the value at the address
		return ((regMap[bitField.posMap] & bitField.mask) >> bitField.position);
    }

    // Not able to read that address, just return a -1
    return -1;
}


// Write out a bit field to the register
void setBitField(BitField_t bitField, uint16_t bitFNewValue) {
	if ((REG_ACCESS_W & bitField.regAccess) == REG_ACCESS_W) {
		regMap[bitField.posMap] = (regMap[bitField.posMap] & ~bitField.mask) | ((bitFNewValue << bitField.position) & bitField.mask);
		writeToEncoderRegister(addrFields[bitField.posMap].regAddress, regMap[bitField.posMap]);
	}
}


// Reads the state of the encoder
uint16_t readEncoderState() {
    return (readEncoderRegister(ENCODER_STATUS_REG));
}


// Reads the value for the angle of the encoder (ranges from 0-360)
double getAngle(bool average) {

    // Get the value of the angle register
    uint16_t rawData = readEncoderRegister(ENCODER_ANGLE_REG);

    // Delete the first bit, saving the last 15
    rawData = (rawData & (DELETE_BIT_15));

    // Add the averaged value (equation from TLE5012 library)
    double angle = ((360.0 / POW_2_15) * ((double) rawData)) - startupAngleOffset;
    encoderAngleAvg.add(angle);

    // Return the average if desired, otherwise just the raw angle
    if (average) {
        return encoderAngleAvg.get();
    }
    else {
        return angle;
    }
}

// For average velocity calculations instead of hardware readings from the TLE5012
#ifdef ENCODER_SPEED_ESTIMATION

// Reads the speed of the encoder (for later)
// ! Needs fixed yet, readings are off
double getEncoderSpeed() {

    // Get the newest angle
    double newAngle = getAbsoluteAngle();

    // Sample time
    uint32_t currentTime = micros();

    // Compute the average velocity
    double avgVelocity = 1000000 * (newAngle - lastEncoderAngle) / (currentTime - lastAngleSampleTime);

    // Correct the last angle and sample time
    lastEncoderAngle = newAngle;
    lastAngleSampleTime = currentTime;

    // Add the value to the averaging list
    encoderSpeedAvg.add(avgVelocity);

    // Return the averaged velocity
    return encoderSpeedAvg.get();
}

#else // ENCODER_ESTIMATION

// Reads the speed of the encoder (for later)
double getEncoderSpeed() {

    // Prepare the variables to store data in
	uint16_t rawData[4];

    // Read the encoder, modifying the array
    readMultipleEncoderRegisters(ENCODER_SPEED_REG, rawData, sizeof(rawData) / sizeof(uint16_t));

	// Get raw speed reading
	int16_t rawSpeed = rawData[0];
	rawSpeed = rawSpeed & DELETE_BIT_15;

	// If bit 14 is set, the value is negative
	if (rawSpeed & CHECK_BIT_14) {
		rawSpeed = rawSpeed - CHANGE_UINT_TO_INT_15;
	}

	// Get FIR_MD from bits 15 and 16 of register 0x06
	uint16_t firMD = rawData[3] >> 14;

	// Determine sensor update rate from FIR_MD
	double firMDVal;
    switch (firMD) {
        case 0:
            firMDVal = 21.3;
            break;
        case 1:
            firMDVal = 42.7;
            break;
        case 2:
            firMDVal = 85.3;
            break;
        case 3:
            firMDVal = 170.6;
            break;
        default:
            firMDVal = 0.0;
            break;
    }

    // Calculate and return average angle speed in degree per second
	encoderSpeedAvg.add(((360.0 / POW_2_15) * rawSpeed) / (2.0 * firMDVal * 0.000001));
    return encoderSpeedAvg.get();
}

#endif // ! ENCODER_ESTIMATION

// Reads the temperature of the encoder
double getEncoderTemp() {

    // Create a variable to store the raw encoder data in
    uint16_t rawData = readEncoderRegister(ENCODER_TEMP_REG);

    // Delete everything before the first 7 bits
	rawData = (rawData & (DELETE_7_BITS));

	// Check if the value received is positive or negative
	if (rawData & CHECK_BIT_9) {
		rawData = rawData - CHANGE_UNIT_TO_INT_9;
	}

    // Return the value (equation from TLE5012 library)
	encoderTempAvg.add(((int16_t)rawData + TEMP_OFFSET) / (TEMP_DIV));
    return encoderTempAvg.get();
}


// Gets the absolute revolutions of the motor
double getAbsoluteRev() {

    // Get the angle value
    int16_t rawData = readEncoderRegister(ENCODER_ANGLE_REV_REG);

    // Delete the first 7 bits, they are not needed
	rawData = (rawData & (DELETE_7_BITS));

    // Check if the value is negative, if so it needs 512 subtracted from it
    if (rawData & CHECK_BIT_9) {
        rawData -= 512;
    }

    // Return the angle measurement
    return ((double)rawData - startupAngleRevOffset);
}


// Gets the absolute angle of the motor
double getAbsoluteAngle() {
    encoderAbsoluteAngleAvg.add((getAbsoluteRev() * 360) + getAngle(false));
    return encoderAbsoluteAngleAvg.get();
}


// Set encoder zero point
void zeroEncoder() {
    startupAngleOffset = getAngle() + startupAngleOffset;
    startupAngleRevOffset = getAbsoluteRev() + startupAngleRevOffset;
}