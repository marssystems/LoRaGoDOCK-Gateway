/******************************************************************************************
 *
 * Description: Source code for single-channel LoRaWAN Gateway based on ESP8266 and SX1276
 * Version    : 0.8.0
 * Date       : 2017-10-19
 * Software   : https://github.com/SandboxElectronics/LoRaGoDOCK-Gateway
 * Hardware   : LoRaGo DOCK – http://sandboxelectronics.com/?product=lorago-dock-single-channel-lorawan-gateway
 * 
 * Copyright (c) 2016, 2017 Maarten Westenberg
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the MIT License
 * which accompanies this distribution, and is available at
 * https://opensource.org/licenses/mit-license.php
 *
 *****************************************************************************************/
 
// This file contains a number of compile-time settings and declaations that are
// specific to the LoRa rfm95, sx1276, sx1272 radio of the gateway.
//
//
// ------------------------------------------------------------------------------------

// Our code should correct the server timing
long txDelay= 0x00;								// delay time on top of server TMST
#define SPIFREQ  10000000
#define SPISPEED 10000000						// was 50000/50KHz < 10MHz

// Frequencies
// Set center frequency. If in doubt, choose the first one, comment all others
// Each "real" gateway should support the first 3 frequencies according to LoRa spec.

int freqs [] = { 
	868100000, 									// Channel 0, 868.1 MHz primary
	868300000, 									// Channel 1, 868.3 MHz mandatory
	868500000, 									// Channel 2, 868.5 MHz mandatory
	867100000, 									// Channel 3, 867.1 MHz
	867300000, 
	867500000, 
	867700000, 
	867900000, 
	868800000, 
	869525000									// Channel, for responses gateway (10%)
	// TTN defines an additional channel at 869.525Mhz using SF9 for class B. Not used
};
uint32_t  freq = freqs[0];
uint8_t	 ifreq = 0;								// Channel Index



// Set the structure for spreading factor
enum sf_t { SF6=6, SF7, SF8, SF9, SF10, SF11, SF12 };

// The state of the receiver. See Semtech Datasheet (rev 4, March 2015) page 43
// The _state is of the enum type (and should be cast when used as a number)
enum state_t { S_INIT=0, S_SCAN, S_CAD, S_RX, S_RXDONE, S_TX };
state_t _state;

// rssi is measured at specific moments and reported on others
// so we need to store the current value we like to work with
uint8_t _rssi;	

// In order to make the CAD behaviour dynamic we set a variable
// when the CAD functions are defined. Value of 3 is minimum frequencies a
// gateway should support to be fully LoRa compliant.
#define NUM_HOPS 3

bool _cad= (bool) _CAD;	// Set to true for Channel Activity Detection, only when dio1 connected
bool _hop=false;		// experimental; frequency hopping. Only use when dio2 connected
bool inHop=false;
unsigned long nowTime=0;
unsigned long hopTime=0;

// Define whether we shoudl include protection for the interrupt()
// routine to make it re-entrant (a second interrupt while still in first
// invocation of the interrupt routine
//	Assuming we need to protect against re entrance of interrupt handler
//	bute we need not necessarily enable full re-entrance, 1 is a good value!
#define REENTRANT 2
#if REENTRANT>=1
// Declare a variable that should keep track of us handling an interrupt.
volatile bool inIntr = 0;
#endif

// Definition of the GPIO pins used by the Gateway
struct pins {
	uint8_t dio0 = 15;  // GPIO15 - For the Hallard board shared between DIO0/DIO1/DIO2
	uint8_t dio1 = 15;  // GPIO15 - Used for CAD, may or not be shared with DIO0
	uint8_t dio2 = 15;  // GPIO15 - Used for frequency hopping, don't care
	uint8_t ss   = 16;  // GPIO16 - Slave Select pin
	uint8_t rst  = 0;   // GPIO0  - Reset pin not used
} pins;

// STATR contains the statictis that are kept by message. 
// Ech time a message is received or sent the statistics are updated.
// In case STATISTICS==1 we define the last MAX_STAT messages as statistics
#if STATISTICS >= 1
struct stat_t {
	unsigned long tmst;						// Time since 1970			
	unsigned long node;						// 4-byte DEVaddr (the only one known to gateway)
	uint8_t ch;								// Channel index to freqs array
	uint8_t sf;
	int8_t rssi;							// XXX Can be < -128
	int8_t prssi;							// XXX Can be < -128	
} stat_t;
// History of received uplink messages from nodes
struct stat_t statr[MAX_STAT];

// STATC contains the statistic that are gateway related and not per
// message. Example: Number of messages received on SF7 or number of (re) boots
// So where statr contains the statistics gathered per packet the statc
// contains general statics of the node
#if STATISTICS >= 2							// Only if we explicitely set it higher
struct stat_c {
	unsigned long sf7;						// Spreading factor 7
	unsigned long sf8;						// Spreading factor 8
	unsigned long sf9;						// Spreading factor 9
	unsigned long sf10;						// Spreading factor 10
	unsigned long sf11;						// Spreading factor 11
	unsigned long sf12;						// Spreading factor 12
	
	uint16_t boots;							// Number of boots
	uint16_t resets;
} stat_c;
struct stat_c statc;
#endif
#endif

// Interrupt variables
volatile uint8_t flags;
volatile uint8_t mask;

// Do not change these setting for RSSI detection. They are used for CAD
// Given the correction factor of 157, we can get to -118dB with this rating
// XXX 40 works
#define RSSI_LIMIT	39
#define RSSI_LIMIT_DOWN 34						// Was 34
// How long to wait in LoRa mode before using the RSSSI value. 
// XXX 275 works well in old CAD mode
#define RSSI_WAIT	250
#define RSSI_WAIT_DOWN 225

// ============================================================================
// Set all definitions for Gateway
// ============================================================================	
// Register definitions. These are the addresses of the TFM95, SX1276 that we 
// need to set in the program.

#define REG_FIFO                    0x00
#define REG_OPMODE                  0x01
// Register 2 to 5 are unused for LoRa
#define REG_FRF_MSB					0x06
#define REG_FRF_MID					0x07
#define REG_FRF_LSB					0x08
#define REG_PAC                     0x09
#define REG_PARAMP                  0x0A
#define REG_LNA                     0x0C
#define REG_FIFO_ADDR_PTR           0x0D
#define REG_FIFO_TX_BASE_AD         0x0E
#define REG_FIFO_RX_BASE_AD         0x0F

#define REG_FIFO_RX_CURRENT_ADDR    0x10
#define REG_IRQ_FLAGS_MASK          0x11
#define REG_IRQ_FLAGS               0x12
#define REG_RX_NB_BYTES             0x13
#define REG_PKT_SNR_VALUE			0x19
#define REG_PKT_RSSI				0x1A	// latest package
#define REG_RSSI					0x1B	// Current RSSI, section 6.4, or  5.5.5
#define REG_HOP_CHANNEL				0x1C
#define REG_MODEM_CONFIG1           0x1D
#define REG_MODEM_CONFIG2           0x1E
#define REG_SYMB_TIMEOUT_LSB  		0x1F

#define REG_PAYLOAD_LENGTH          0x22
#define REG_MAX_PAYLOAD_LENGTH 		0x23
#define REG_HOP_PERIOD              0x24
#define REG_MODEM_CONFIG3           0x26
#define REG_RSSI_WIDEBAND			0x2C

#define REG_INVERTIQ				0x33
#define REG_DET_TRESH				0x37				// SF6
#define REG_SYNC_WORD				0x39
#define REG_TEMP					0x3C

#define REG_DIO_MAPPING_1           0x40
#define REG_DIO_MAPPING_2           0x41
#define REG_VERSION	  				0x42

#define REG_PADAC					0x5A
#define REG_PADAC_SX1272			0x5A
#define REG_PADAC_SX1276			0x4D

// ----------------------------------------
// Used by REG_PAYLOAD_LENGTH to set receive patyload length
#define PAYLOAD_LENGTH              0x40

// ----------------------------------------
// opModes
#define SX72_MODE_SLEEP             0x80
#define SX72_MODE_STANDBY           0x81
#define SX72_MODE_FSTX              0x82
#define SX72_MODE_TX                0x83	// 0x80 | 0x03
#define SX72_MODE_RX_CONTINUOS      0x85

// ----------------------------------------
// LMIC Constants for radio registers
#define OPMODE_LORA      			0x80
#define OPMODE_MASK      			0x07
#define OPMODE_SLEEP     			0x00
#define OPMODE_STANDBY   			0x01
#define OPMODE_FSTX      			0x02
#define OPMODE_TX        			0x03
#define OPMODE_FSRX      			0x04
#define OPMODE_RX        			0x05
#define OPMODE_RX_SINGLE 			0x06
#define OPMODE_CAD       			0x07



// ----------------------------------------
// LOW NOISE AMPLIFIER

#define LNA_MAX_GAIN                0x23
#define LNA_OFF_GAIN                0x00
#define LNA_LOW_GAIN		    	0x20

// CONF REG
#define REG1                        0x0A
#define REG2                        0x84

// ----------------------------------------
// MC1 sx1276 RegModemConfig1
#define SX1276_MC1_BW_125           0x70
#define SX1276_MC1_BW_250           0x80
#define SX1276_MC1_BW_500           0x90
#define SX1276_MC1_CR_4_5           0x02
#define SX1276_MC1_CR_4_6           0x04
#define SX1276_MC1_CR_4_7           0x06
#define SX1276_MC1_CR_4_8           0x08
#define SX1276_MC1_IMPLICIT_HEADER_MODE_ON  0x01

#define SX72_MC1_LOW_DATA_RATE_OPTIMIZE     0x01 	// mandated for SF11 and SF12

// ----------------------------------------
// MC2 definitions
#define SX72_MC2_FSK                0x00
#define SX72_MC2_SF7                0x70		// SF7 == 0x07, so (SF7<<4) == SX7_MC2_SF7
#define SX72_MC2_SF8                0x80
#define SX72_MC2_SF9                0x90
#define SX72_MC2_SF10               0xA0
#define SX72_MC2_SF11               0xB0
#define SX72_MC2_SF12               0xC0

// ----------------------------------------
// MC3
#define SX1276_MC3_LOW_DATA_RATE_OPTIMIZE  0x08
#define SX1276_MC3_AGCAUTO                 0x04

// ----------------------------------------
// FRF
#define FRF_MSB						0xD9		// 868.1 Mhz
#define FRF_MID						0x06
#define FRF_LSB						0x66

// ----------------------------------------
// DIO function mappings           		     D0D1D2D3
#define MAP_DIO0_LORA_RXDONE   		0x00  // 00------ bit 7 and 6
#define MAP_DIO0_LORA_TXDONE   		0x40  // 01------
#define MAP_DIO0_LORA_CADDONE  		0x80  // 10------
#define MAP_DIO0_LORA_NOP   		0xC0  // 11------

#define MAP_DIO1_LORA_RXTOUT   		0x00  // --00---- bit 5 and 4
#define MAP_DIO1_LORA_FCC			0x10  // --01----
#define MAP_DIO1_LORA_CADDETECT		0x20  // --10----
#define MAP_DIO1_LORA_NOP      		0x30  // --11----

#define MAP_DIO2_LORA_FCC0      	0x00  // ----00-- bit 3 and 2
#define MAP_DIO2_LORA_FCC1      	0x04  // ----01-- bit 3 and 2
#define MAP_DIO2_LORA_FCC2      	0x08  // ----10-- bit 3 and 2
#define MAP_DIO2_LORA_NOP      		0x0C  // ----11-- bit 3 and 2

#define MAP_DIO3_LORA_CADDONE  		0x00  // ------00 bit 1 and 0
#define MAP_DIO3_LORA_NOP      		0x03  // ------11

#define MAP_DIO0_FSK_READY     		0x00  // 00------ (packet sent / payload ready)
#define MAP_DIO1_FSK_NOP       		0x30  // --11----
#define MAP_DIO2_FSK_TXNOP     		0x04  // ----01--
#define MAP_DIO2_FSK_TIMEOUT   		0x08  // ----10--

// ----------------------------------------
// Bits masking the corresponding IRQs from the radio
#define IRQ_LORA_RXTOUT_MASK 		0x80
#define IRQ_LORA_RXDONE_MASK 		0x40
#define IRQ_LORA_CRCERR_MASK 		0x20
#define IRQ_LORA_HEADER_MASK 		0x10
#define IRQ_LORA_TXDONE_MASK 		0x08
#define IRQ_LORA_CDDONE_MASK 		0x04
#define IRQ_LORA_FHSSCH_MASK 		0x02
#define IRQ_LORA_CDDETD_MASK 		0x01


// ----------------------------------------
// Definitions for UDP message arriving from server
#define PROTOCOL_VERSION			0x01
#define PKT_PUSH_DATA				0x00
#define PKT_PUSH_ACK				0x01
#define PKT_PULL_DATA				0x02
#define PKT_PULL_RESP				0x03
#define PKT_PULL_ACK				0x04
#define PKT_TX_ACK                  0x05

#define MGT_RESET					0x15	// Not a LoRa Gateway Spec message
#define MGT_SET_SF					0x16
#define MGT_SET_FREQ				0x17

