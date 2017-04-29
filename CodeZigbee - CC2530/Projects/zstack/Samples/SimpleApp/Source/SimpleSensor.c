
/******************************************************************************
 * INCLUDES
 */

#include "ZComDef.h"
#include "OSAL.h"
#include "sapi.h"
#include "hal_key.h"
#include "hal_led.h"
#include "hal_adc.h"
#include "hal_mcu.h"
#include "SimpleApp.h"
#include "hal_uart.h"

/*********************************************************************
 * CONSTANTS
 */
#define MY_ID  (uint8)2
// Application States
#define APP_INIT                           0    // Initial state
#define APP_START                          1    // Sensor has joined network
#define APP_BOUND                          2    // Sensor is bound to collector

// Application osal event identifiers
// Bit mask of events ( from 0x0000 to 0x00FF )
#define MY_START_EVT                0x0001
#define MY_REPORT_TEMP_EVT          0x0002
#define MY_REPORT_BATT_EVT          0x0004
#define MY_FIND_COLLECTOR_EVT       0x0008
#define ID_REPORT_EVT               0x0010

// ADC definitions for CC2430/CC2530 from the hal_adc.c file
#if defined (HAL_MCU_CC2430) || defined (HAL_MCU_CC2530)
#define HAL_ADC_REF_125V    0x00    /* Internal 1.25V Reference */
#define HAL_ADC_DEC_064     0x00    /* Decimate by 64 : 8-bit resolution */
#define HAL_ADC_DEC_128     0x10    /* Decimate by 128 : 10-bit resolution */
#define HAL_ADC_DEC_512     0x30    /* Decimate by 512 : 14-bit resolution */
#define HAL_ADC_CHN_VDD3    0x0f    /* Input channel: VDD/3 */
#define HAL_ADC_CHN_TEMP    0x0e    /* Temperature sensor */
#endif //HAL_MCU_CC2430 || HAL_MCU_CC2530

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

static uint8 myAppState = APP_INIT;

static uint16 myStartRetryDelay = 5000;      // milliseconds
static uint16 myTempReportPeriod = 5000;     // milliseconds
static uint16 myBatteryCheckPeriod = 15000;   // milliseconds
static uint16 myBindRetryDelay = 4000;       // milliseconds
static uint16 myIdReportPeriod = 4000;     // milliseconds

static void Uart0_Cb(uint8 port, uint8 event);

/*********************************************************************
 * GLOBAL VARIABLES
 */

// Inputs and Outputs for Sensor device
#define NUM_OUT_CMD_SENSOR                1
#define NUM_IN_CMD_SENSOR                 1

// List of output and input commands for Sensor device
const cId_t zb_OutCmdList[NUM_OUT_CMD_SENSOR] =
{
  SENSOR_REPORT_CMD_ID
};

const cId_t zb_InCmdList[NUM_IN_CMD_SENSOR] =
{
  CTRL_PUMP_CMD_ID
};

#define TEMP_REPORT     0x01
#define BATTERY_REPORT  0x02
#define ID_REPORT       0x04

// Define SimpleDescriptor for Sensor device
const SimpleDescriptionFormat_t zb_SimpleDesc =
{
  MY_ENDPOINT_ID,             //  Endpoint
  MY_PROFILE_ID,              //  Profile ID
  DEV_ID_SENSOR,              //  Device ID
  DEVICE_VERSION_SENSOR,      //  Device Version
  0,                          //  Reserved
  NUM_IN_CMD_SENSOR,          //  Number of Input Commands
  (cId_t *) zb_InCmdList,             //  Input Command List
  NUM_OUT_CMD_SENSOR,         //  Number of Output Commands
  (cId_t *) zb_OutCmdList     //  Output Command List
};


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void myApp_StartReporting( void );
static void myApp_StopReporting( void );

static uint8 myApp_ReadTemperature( void );
static uint8 myApp_ReadBattery( void );

/*****************************************************************************
 * @fn          zb_HandleOsalEvent
 *
 * @brief       The zb_HandleOsalEvent function is called by the operating
 *              system when a task event is set
 *
 * @param       event - Bitmask containing the events that have been set
 *
 * @return      none
 */
void zb_HandleOsalEvent( uint16 event )
{
  static uint8 pData[2];
  uint8 startOptions;
  uint8 logicalType;
  
  
  if ( event & ZB_ENTRY_EVENT )
  {
    halUARTCfg_t uConfig;
    uConfig.configured = TRUE; 
    uConfig.baudRate = HAL_UART_BR_9600;
    uConfig.flowControl = FALSE;
    uConfig.flowControlThreshold = 48;
    uConfig.idleTimeout = 6; 
    uConfig.rx.maxBufSize = 128;
    uConfig.tx.maxBufSize = 128;      
    uConfig.intEnable = TRUE;//enable interrupts
    uConfig.callBackFunc = &Uart0_Cb;
    HalUARTOpen(HAL_UART_PORT_0,&uConfig);
    HalUARTWrite(HAL_UART_PORT_0,"\nENTRY\n", (byte)osal_strlen("\nENTRY\n")); 
    
    startOptions = ZCD_STARTOPT_CLEAR_STATE | ZCD_STARTOPT_CLEAR_CONFIG;    
    zb_WriteConfiguration( ZCD_NV_STARTUP_OPTION, sizeof(uint8), &startOptions );
    
    logicalType = ZG_DEVICETYPE_ENDDEVICE;
    zb_WriteConfiguration(ZCD_NV_LOGICAL_TYPE, sizeof(uint8), &logicalType);
    
    zb_ReadConfiguration( ZCD_NV_STARTUP_OPTION, sizeof(uint8), &startOptions );
    startOptions = ZCD_STARTOPT_AUTO_START;
    zb_WriteConfiguration( ZCD_NV_STARTUP_OPTION, sizeof(uint8), &startOptions );
    
    zb_StartRequest();
  }
  if ( event & MY_REPORT_TEMP_EVT )
  {
    pData[0] = TEMP_REPORT;
    pData[1] =  myApp_ReadTemperature();
    zb_SendDataRequest( 0xFFFE, SENSOR_REPORT_CMD_ID, 2, pData, 0, AF_ACK_REQUEST, 0 );
    //0xFFFE Gui toi thiet bi dang Bind
    //HalUARTWrite(HAL_UART_PORT_0,"REPORT_TEMP\n", (byte)osal_strlen("REPORT_TEMP\n")); 
    osal_start_timerEx( sapi_TaskID, MY_REPORT_TEMP_EVT, myTempReportPeriod );
  }

  if ( event & MY_REPORT_BATT_EVT )
  {
    pData[0] = BATTERY_REPORT;
    pData[1] =  myApp_ReadBattery();
    zb_SendDataRequest( 0xFFFE, SENSOR_REPORT_CMD_ID, 2, pData, 0, AF_ACK_REQUEST, 0 );
    //HalUARTWrite(HAL_UART_PORT_0,"REPORT_BATT\n", (byte)osal_strlen("REPORT_BATT\n")); 
    osal_start_timerEx( sapi_TaskID, MY_REPORT_BATT_EVT, myBatteryCheckPeriod );
  }

  if ( event & MY_FIND_COLLECTOR_EVT )
  {
    //HalUARTWrite(HAL_UART_PORT_0,"FIND_COLLECTOR\n", (byte)osal_strlen("FIND_COLLECTOR\n"));  
    zb_BindDevice( TRUE, SENSOR_REPORT_CMD_ID, (uint8 *)NULL );
  }
  
  if ( event & ID_REPORT_EVT )
  {
    pData[0] = ID_REPORT;
    pData[1] =  MY_ID;
    zb_SendDataRequest( 0xFFFE, SENSOR_REPORT_CMD_ID, 2, pData, 0, AF_ACK_REQUEST, 0 );
    //0xFFFE Gui toi thiet bi dang Bind
    //HalUARTWrite(HAL_UART_PORT_0,"REPORT_ID\n", (byte)osal_strlen("REPORT_ID\n")); 
    osal_start_timerEx( sapi_TaskID, ID_REPORT_EVT, myIdReportPeriod );
  }

}
/*********************************************************************
 * @fn      zb_HandleKeys
 *
 * @brief   Handles all key events for this device.
 *
 * @param   shift - true if in shift/alt.
 * @param   keys - bit field for key events. Valid entries:
 *                 EVAL_SW4
 *                 EVAL_SW3
 *                 EVAL_SW2
 *                 EVAL_SW1
 *
 * @return  none
 */
void zb_HandleKeys( uint8 shift, uint8 keys )
{
  return;
}
/******************************************************************************
 * @fn          zb_StartConfirm
 *
 * @brief       The zb_StartConfirm callback is called by the ZigBee stack
 *              after a start request operation completes
 *
 * @param       status - The status of the start operation.  Status of
 *                       ZB_SUCCESS indicates the start operation completed
 *                       successfully.  Else the status is an error code.
 *
 * @return      none
 */
void zb_StartConfirm( uint8 status )
{
  if ( status == ZB_SUCCESS )
  {
    myAppState = APP_START;

    // Set event to bind to a collector
    osal_start_timerEx( sapi_TaskID, MY_FIND_COLLECTOR_EVT, myBindRetryDelay );
  }
  else
  {
    // Try joining again later with a delay
    osal_start_timerEx( sapi_TaskID, MY_START_EVT, myStartRetryDelay );
  }
}
/******************************************************************************
 * @fn          zb_SendDataConfirm
 *
 * @brief       The zb_SendDataConfirm callback function is called by the
 *              ZigBee after a send data operation completes
 *
 * @param       handle - The handle identifying the data transmission.
 *              status - The status of the operation.
 *
 * @return      none
 */
void zb_SendDataConfirm( uint8 handle, uint8 status )
{
  (void)handle; 
  
  //HalUARTWrite(HAL_UART_PORT_0,"SendDataConfirm ", (byte)osal_strlen("SendDataConfirm ")); 

  if ( status != ZSuccess )
  {    
    //HalUARTWrite(HAL_UART_PORT_0,"Fail\n", (byte)osal_strlen("Fail\n"));
    // Remove bindings to the existing collector
    zb_BindDevice( FALSE, SENSOR_REPORT_CMD_ID, (uint8 *)NULL );

    myAppState = APP_START;
    myApp_StopReporting();

    // Start process of finding new collector with minimal delay
    osal_start_timerEx( sapi_TaskID, MY_FIND_COLLECTOR_EVT, 1 );
  }
  else
  {
    //HalUARTWrite(HAL_UART_PORT_0,"Success\n", (byte)osal_strlen("Success\n"));
  }
}
/******************************************************************************
 * @fn          zb_BindConfirm
 *
 * @brief       The zb_BindConfirm callback is called by the ZigBee stack
 *              after a bind operation completes.
 *
 * @param       commandId - The command ID of the binding being confirmed.
 *              status - The status of the bind operation.
 *
 * @return      none
 */
void zb_BindConfirm( uint16 commandId, uint8 status )
{
  (void)commandId;
  
  //HalUARTWrite(HAL_UART_PORT_0,"BindConfirm\n", (byte)osal_strlen("BindConfirm\n")); 

  if ( ( status == ZB_SUCCESS ) && ( myAppState == APP_START ) )
  {
    myAppState = APP_BOUND;

    //Start reporting sensor values
    myApp_StartReporting();
  }
  else
  {
    // Continue to discover a collector
    osal_start_timerEx( sapi_TaskID, MY_FIND_COLLECTOR_EVT, myBindRetryDelay );
  }
}
/******************************************************************************
 * @fn          zb_AllowBindConfirm
 *
 * @brief       Indicates when another device attempted to bind to this device
 *
 * @param
 *
 * @return      none
 */
void zb_AllowBindConfirm( uint16 source )
{
  (void)source;
  //HalUARTWrite(HAL_UART_PORT_0,"AllowBindConfirm\n", (byte)osal_strlen("AllowBindConfirm\n"));
}
/******************************************************************************
 * @fn          zb_FindDeviceConfirm
 *
 * @brief       The zb_FindDeviceConfirm callback function is called by the
 *              ZigBee stack when a find device operation completes.
 *
 * @param       searchType - The type of search that was performed.
 *              searchKey - Value that the search was executed on.
 *              result - The result of the search.
 *
 * @return      none
 */
void zb_FindDeviceConfirm( uint8 searchType, uint8 *searchKey, uint8 *result )
{
  // Add your code here and remove the "(void)" lines.
  (void)searchType;
  (void)searchKey;
  (void)result;
  //HalUARTWrite(HAL_UART_PORT_0,"FindDeviceConfirm\n", (byte)osal_strlen("FindDeviceConfirm\n"));
}

/******************************************************************************
 * @fn          zb_ReceiveDataIndication
 *
 * @brief       The zb_ReceiveDataIndication callback function is called
 *              asynchronously by the ZigBee stack to notify the application
 *              when data is received from a peer device.
 *
 * @param       source - The short address of the peer device that sent the data
 *              command - The commandId associated with the data
 *              len - The number of bytes in the pData parameter
 *              pData - The data sent by the peer device
 *
 * @return      none
 */
void zb_ReceiveDataIndication( uint16 source, uint16 command, uint16 len, uint8 *pData,int8 r_power  )
{
  //HalUARTWrite(HAL_UART_PORT_0,"ReceiveDataIndication\n", (byte)osal_strlen("ReceiveDataIndication\n")); 
  
}
/******************************************************************************
 * @fn          my_StartReporting
 *
 * @brief       Starts the process to periodically report sensor readings
 *
 * @param
 *
 * @return      none
 */
void myApp_StartReporting( void )
{
  //osal_start_timerEx( sapi_TaskID, MY_REPORT_TEMP_EVT, myTempReportPeriod );
  //osal_start_timerEx( sapi_TaskID, MY_REPORT_BATT_EVT, myBatteryCheckPeriod );
  osal_start_timerEx( sapi_TaskID, ID_REPORT_EVT, myIdReportPeriod );
  //HalLedSet( HAL_LED_1, HAL_LED_MODE_ON );
  
  //HalUARTWrite(HAL_UART_PORT_0,"StartReporting\n", (byte)osal_strlen("StartReporting\n")); 

}
/******************************************************************************
 * @fn          my_StopReporting
 *
 * @brief       Stops the process to periodically report sensor readings
 *
 * @param
 *
 * @return      none
 */
void myApp_StopReporting( void )
{
  //osal_stop_timerEx( sapi_TaskID, MY_REPORT_TEMP_EVT );
  //osal_stop_timerEx( sapi_TaskID, MY_REPORT_BATT_EVT );
  osal_stop_timerEx( sapi_TaskID, ID_REPORT_EVT );
  //HalLedSet( HAL_LED_1, HAL_LED_MODE_OFF );
  
  //HalUARTWrite(HAL_UART_PORT_0,"StopReporting\n", (byte)osal_strlen("StopReporting\n")); 
}
/******************************************************************************
 * @fn          myApp_ReadBattery
 *
 * @brief       Reports battery sensor reading
 *
 * @param
 *
 * @return
 ******************************************************************************/
uint8 myApp_ReadBattery( void )
{

#if defined (HAL_MCU_CC2430) || defined (HAL_MCU_CC2530)

  uint16 value;

  /* Clear ADC interrupt flag */
  ADCIF = 0;

  ADCCON3 = (HAL_ADC_REF_125V | HAL_ADC_DEC_128 | HAL_ADC_CHN_VDD3);

  /* Wait for the conversion to finish */
  while ( !ADCIF );

  /* Get the result */
  value = ADCL;
  value |= ((uint16) ADCH) << 8;

  /*
   * value now contains measurement of Vdd/3
   * 0 indicates 0V and 32767 indicates 1.25V
   * voltage = (value*3*1.25)/32767 volts
   * we will multiply by this by 10 to allow units of 0.1 volts
   */

  value = value >> 6;   // divide first by 2^6
  value = (uint16)(value * 37.5);
  value = value >> 9;   // ...and later by 2^9...to prevent overflow during multiplication

  return value;

#endif    // CC2430 or CC2530

#if defined HAL_MCU_MSP430

  uint16 value;

/*
  There are more than MSP430 board now. Idealy, ADC read should be called
*/
#if defined (HAL_BOARD_F5438)

  value = HalAdcRead (HAL_ADC_CHANNEL_VDD, HAL_ADC_RESOLUTION_14);
  value = value * 50;
  value = value / 4096;

#else

  ADC12CTL0 = ADC12ON+SHT0_2+REFON;             // Turn on and set up ADC12
  ADC12CTL1 = SHP;                              // Use sampling timer
  ADC12MCTL0 = SREF_1+INCH_11;                  // Vr+=Vref+

  ADC12CTL0 |= ENC | ADC12SC;                   // Start conversion
  while ((ADC12IFG & BIT0)==0);

  value = ADC12MEM0;

  /*
   * value now contains measurement of AVcc/2
   * value is in range 0 to 4095 indicating voltage from 0 to 1.5V
   * voltage = (value*2*1.5)/4095 volts
   * we will multiply by this by 10 to allow units of 0.1 volts
   */

  value = value >> 1;     // value is now in range of 0 to 2048
  value = value * 30;
  value = value >> 11;

#endif

  return ( value );

#endif // MSP430

#if defined HAL_MCU_AVR

  // If platform doesnt support a battery sensor, just return random value

  uint8 value;
  value = 20 + ( osal_rand() & 0x000F );
  return ( value );

#endif  // AVR

}
/******************************************************************************
 * @fn          myApp_ReadTemperature
 *
 * @brief       Reports temperature sensor reading
 *
 * @param
 *
 * @return
 ******************************************************************************/
uint8 myApp_ReadTemperature( void )
{

#if defined (HAL_MCU_CC2430) || defined (HAL_MCU_CC2530)

  uint16 value;

  /* Clear ADC interrupt flag */
  ADCIF = 0;

  ADCCON3 = (HAL_ADC_REF_125V | HAL_ADC_DEC_512 | HAL_ADC_CHN_TEMP);

  /* Wait for the conversion to finish */
  while ( !ADCIF );

  /* Get the result */
  value = ADCL;
  value |= ((uint16) ADCH) << 8;

  /*
   * value ranges from 0 to 0x8000 indicating 0V and 1.25V
   * VOLTAGE_AT_TEMP_ZERO = 0.743 V = 19477
   * TEMP_COEFFICIENT = 0.0024 V/C = 62.9 /C
   * These parameters are typical values and need to be calibrated
   * See the datasheet for the appropriate chip for more details
   * also, the math below may not be very accurate
   */
#if defined (HAL_MCU_CC2430)
  #define VOLTAGE_AT_TEMP_ZERO      19477   // 0.743 V
  #define TEMP_COEFFICIENT          62.9    // 0.0024 V/C
#elif defined (HAL_MCU_CC2530)
    /* Assume ADC = 5158 at 0C and ADC = 15/C */
  #define VOLTAGE_AT_TEMP_ZERO      5158
  #define TEMP_COEFFICIENT          14
#endif

  // limit min temp to 0 C
  if ( value < VOLTAGE_AT_TEMP_ZERO )
    value = VOLTAGE_AT_TEMP_ZERO;

  value = value - VOLTAGE_AT_TEMP_ZERO;

  // limit max temp to 99 C
  if ( value > TEMP_COEFFICIENT * 99 )
    value = TEMP_COEFFICIENT * 99;

  return ( (uint8)(value/TEMP_COEFFICIENT) );

#endif  // CC2430 || CC2530


#if defined HAL_MCU_MSP430

  uint16 value;

/*
  There are more than MSP430 board now. Idealy, ADC read should be called
*/
#if defined (HAL_BOARD_F5438)

  long multiplier, offset;

  value = HalAdcRead (HAL_ADC_CHANNEL_TEMP, HAL_ADC_RESOLUTION_14);

  multiplier = (long) 7040 * 9 /5 ;
  offset = (long) 2620 * 9 / 5 - 320;

  value = (long) value * multiplier/4096 - offset;

  return (value);

#else
  ADC12CTL0 = ADC12ON+SHT0_7+REFON;         // Turn on and set up ADC12
  ADC12CTL1 = SHP;                          // Use sampling timer
  ADC12MCTL0 = SREF_1+INCH_10;              // Vr+=Vref+

  ADC12CTL0 |= ENC | ADC12SC;               // Start conversion
  while ((ADC12IFG & BIT0)==0);

  value = ADC12MEM0;

  /*
   * value ranges from 0 to 0x0FFF indicating 0V and 1.5V
   * VOLTAGE_AT_TEMP_ZERO = 0.986 V = 2692
   * TEMP_COEFFICIENT = 0.00355 V/C = 9.69 /C
   * These parameters are typical values and need to be calibrated
   * See the datasheet for the appropriate chip for more details
   * also, the math below is not very accurate
   */

#define VOLTAGE_AT_TEMP_ZERO      2692      // 0.986 V
#define TEMP_COEFFICIENT          9.69      // 0.00355 V/C

  // limit min temp to 0 C
  if ( value < VOLTAGE_AT_TEMP_ZERO )
    value = VOLTAGE_AT_TEMP_ZERO;

  value = value - VOLTAGE_AT_TEMP_ZERO;

  // limit max temp to 99 C
  if ( value > (uint16)(TEMP_COEFFICIENT * 99.0) )
    value = (uint16)(TEMP_COEFFICIENT * 99.0);

  return ( (uint8)(value/TEMP_COEFFICIENT) );
#endif // HAL_BOARD_F5438

#endif // MSP430

#if defined HAL_MCU_AVR

  // If platform doesnt support a temperature sensor, just return random value
  uint8 value;
  value = 20 + ( osal_rand() & 0x000F );
  return ( value );

#endif  // AVR

}

static void Uart0_Cb(uint8 port, uint8 event){
  uint8  ch;
  uint8 startOptions;
  if ((event&HAL_UART_RX_TIMEOUT) || (event&HAL_UART_RX_ABOUT_FULL)){    
    while (Hal_UART_RxBufLen(port))
    {
      HalUARTRead ( port, &ch, 1);
      if( ch == '?' ){
        HalUARTWrite(HAL_UART_PORT_0,"\nEndDevice:", (byte)osal_strlen("\nEndDevice:"));  
        if(MY_ID==1)
          HalUARTWrite(HAL_UART_PORT_0,"1\n", (byte)osal_strlen("1\n"));  
        else if(MY_ID==2)
          HalUARTWrite(HAL_UART_PORT_0,"2\n", (byte)osal_strlen("2\n"));  
        else if(MY_ID==3)
          HalUARTWrite(HAL_UART_PORT_0,"3\n", (byte)osal_strlen("3\n"));  
        
      }else if( ch == 'r' ){
          startOptions = ZCD_STARTOPT_CLEAR_STATE | ZCD_STARTOPT_CLEAR_CONFIG;
          zb_WriteConfiguration( ZCD_NV_STARTUP_OPTION, sizeof(uint8), &startOptions );
          zb_SystemReset();
      }else if( ch == '3' ){        
        
      }
    }
  }
}
