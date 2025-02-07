/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2013 Semtech

Description: MCU RTC timer and low power modes management

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis and Gregory Cristian
*/
#include "stm32f0xx_hal.h"
#include <math.h>
#include "board.h"
#include "rtc-board.h"

/*!
 * RTC Time base in ms
 */
 //根据Fck_spre = Frtcclk/(PREDIV_S+1)/(PREDIV_A+1)
//例程中RTC的工作频率为32.778/(8+1)/(1+1) = 37/18 ~= 2.055Khz, 1/2.055 ~= 0.486618
//RTC时钟模块对输入的2.048/2.055kHz脉冲进行计数
#define RTC_ALARM_TICK_DURATION                     0.48840048       // 1 tick every 488us
#define RTC_ALARM_TICK_PER_MS                       2.0475             // 1/2.048 = tick duration in ms
	// sub-second number of bits
#define N_PREDIV_S                                  10

// Synchronous prediv
#define PREDIV_S                                    ( ( 1 << N_PREDIV_S ) - 1 )
/*!
 * Maximum number of days that can be handled by the RTC alarm counter before overflow.
 */
#define RTC_ALARM_MAX_NUMBER_OF_DAYS                28

/*!
 * Number of seconds in a minute
 */
static const uint8_t SecondsInMinute = 60;

/*!
 * Number of seconds in an hour
 */
static const uint16_t SecondsInHour = 3600;

/*!
 * Number of seconds in a day
 */
static const uint32_t SecondsInDay = 86400;

/*!
 * Number of hours in a day
 */
static const uint8_t HoursInDay = 24;

/*!
 * Number of seconds in a leap year
 */
static const uint32_t SecondsInLeapYear = 31622400;

/*!
 * Number of seconds in a year
 */
static const uint32_t SecondsInYear = 31536000;

/*!
 * Number of days in each month on a normal year
 */
static const uint8_t DaysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

/*!
 * Number of days in each month on a leap year
 */
static const uint8_t DaysInMonthLeapYear[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

/*!
 * Holds the current century for real time computation
 */
static uint16_t Century = 0;

/*!
 * Flag used to indicates a Calendar Roll Over is about to happen
 */
static bool CalendarRollOverReady = false;

/*!
 * Flag used to indicates a the MCU has waken-up from an external IRQ
 */
volatile bool NonScheduledWakeUp = false;

/*!
 * RTC timer context
 */
typedef struct RtcCalendar_s
{
    uint16_t CalendarCentury;     //! Keep track of century value
    RTC_DateTypeDef CalendarDate; //! Reference time in calendar format
    RTC_TimeTypeDef CalendarTime; //! Reference date in calendar format
} RtcCalendar_t;

/*!
 * Current RTC timer context
 */
RtcCalendar_t RtcCalendarContext;

/*!
 * \brief Flag to indicate if the timestamps until the next event is long enough
 * to set the MCU into low power mode
 */
static bool RtcTimerEventAllowsLowPower = false;

/*!
 * \brief Flag to disable the LowPower Mode even if the timestamps until the
 * next event is long enough to allow Low Power mode
 */
static bool LowPowerDisableDuringTask = false;

/*!
 * \brief RTC Handler
 */
RTC_HandleTypeDef RtcHandle = { 0 };

/*!
 * \brief Indicates if the RTC is already Initialized or not
 */
static bool RtcInitialized = false;

/*!
 * \brief Indicates if the RTC Wake Up Time is calibrated or not
 */
static bool WakeUpTimeInitialized = false;

/*!
 * \brief Hold the Wake-up time duration in ms
 */
volatile uint32_t McuWakeUpTime = 0;

/*!
 * \brief Hold the cumulated error in micro-second to compensate the timing errors
 */
static int32_t TimeoutValueError = 0;

/*!
 * \brief RTC wakeup time computation
 */
 void RtcComputeWakeUpTime( void );

/*!
 * \brief Start the RTC Alarm (timeoutValue is in ms)
 */
static void RtcStartWakeUpAlarm( uint32_t timeoutValue );

/*!
 * \brief Converts a TimerTime_t value into RtcCalendar_t value
 *
 * \param[IN] timeCounter Value to convert to RTC calendar
 * \retval rtcCalendar New RTC calendar value
 */
//
// REMARK: Removed function static attribute in order to suppress
//         "#177-D function was declared but never referenced" warning.
// static RtcCalendar_t RtcConvertTimerTimeToCalendarTick( TimerTime_t timeCounter )
//
RtcCalendar_t RtcConvertTimerTimeToCalendarTick( TimerTime_t timeCounter );

/*!
 * \brief Converts a RtcCalendar_t value into TimerTime_t value
 *
 * \param[IN/OUT] calendar Calendar value to be converted
 *                         [NULL: compute from "now",
 *                          Others: compute from given calendar value]
 * \retval timerTime New TimerTime_t value
 */
static TimerTime_t RtcConvertCalendarTickToTimerTime( RtcCalendar_t *calendar );

/*!
 * \brief Converts a TimerTime_t value into a value for the RTC Alarm
 *
 * \param[IN] timeCounter Value in ms to convert into a calendar alarm date
 * \param[IN] now Current RTC calendar context
 * \retval rtcCalendar Value for the RTC Alarm
 */
static RtcCalendar_t RtcComputeTimerTimeToAlarmTick( TimerTime_t timeCounter, RtcCalendar_t now );

/*!
 * \brief Returns the internal RTC Calendar and check for RTC overflow
 *
 * \retval calendar RTC calendar
 */
static RtcCalendar_t RtcGetCalendar( void );

/*!
 * \brief Check the status for the calendar year to increase the value of Century at the overflow of the RTC
 *
 * \param[IN] year Calendar current year
 */
static void RtcCheckCalendarRollOver( uint8_t year );

void RtcInit( void )
{
		  RTC_TimeTypeDef sTime = {0};
			RTC_DateTypeDef sDate = {0};
			
			//__HAL_RCC_PWR_CLK_ENABLE();//使能电源时钟PWR
			//HAL_PWR_EnableBkUpAccess();//取消备份区域写保护
			//__HAL_RCC_LSI_ENABLE();

    if( RtcInitialized == false )
    {
			__HAL_RCC_RTC_ENABLE();//RTC时钟使能

			RtcHandle.Instance = RTC;
			RtcHandle.Init.HourFormat = RTC_HOURFORMAT_24;
			RtcHandle.Init.AsynchPrediv = 3;  //PREDIV_A
			RtcHandle.Init.SynchPrediv = 3;   //PREDIV_S
			RtcHandle.Init.OutPut = RTC_OUTPUT_DISABLE;
			RtcHandle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
			RtcHandle.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
			HAL_RTC_Init(&RtcHandle);

			/* USER CODE BEGIN Check_RTC_BKUP */

			/* USER CODE END Check_RTC_BKUP */

			/**Initialize RTC and set the Time and Date 
			*/
			sDate.Year = 0x00;
			sDate.Month = 0x01;
			sDate.Date = 0x01;
			sDate.WeekDay = RTC_WEEKDAY_MONDAY;
			HAL_RTC_SetDate(&RtcHandle, &sDate, RTC_FORMAT_BIN);
		
  		sTime.Hours = 0x00;
			sTime.Minutes = 0x00;
			sTime.Seconds = 0x00;
			sTime.SubSeconds = 0x00;
			//sTime.SecondFraction = 0;
			sTime.TimeFormat = RTC_HOURFORMAT12_AM;
			sTime.StoreOperation = RTC_STOREOPERATION_RESET;
			sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
			HAL_RTC_SetTime(&RtcHandle, &sTime, RTC_FORMAT_BIN);
			// Enable Direct Read of the calendar registers (not through Shadow registers)
      HAL_RTCEx_EnableBypassShadow( &RtcHandle );

			HAL_NVIC_SetPriority( RTC_IRQn, 1, 0 );
			HAL_NVIC_EnableIRQ( RTC_IRQn );

			// Init alarm.
			//HAL_RTC_DeactivateAlarm( &RtcHandle, RTC_ALARM_A );

			//RtcSetTimerContext( );
			RtcInitialized = true;

    }
}
void RtcSetTimeout( uint32_t timeout )
{
    RtcStartWakeUpAlarm( timeout );//此时传入的时间还是obj->Timestamp 1000
}

TimerTime_t RtcGetAdjustedTimeoutValue( uint32_t timeout )
{
	//如果设置定时器超时时间比一个保持唤醒周期还大，需要放到下一个保持唤醒周期进行定时处理
    if( timeout > McuWakeUpTime )
    {   // we have waken up from a GPIO and we have lost "McuWakeUpTime" that we need to compensate on next event
        if( NonScheduledWakeUp == true )
        {
						//DebugPrintf("timeout > McuWakeUpTime\r\nNonScheduledWakeUp == true\r\n");//调试用
            NonScheduledWakeUp = false;
            timeout -= McuWakeUpTime;
        }
    }

    if( timeout > McuWakeUpTime )
    {   // we don't go in Low Power mode for delay below 50ms (needed for LEDs) 低于50ms不允许进入低功耗
        if( timeout < 50 ) // 50 ms
        {
					  //DebugPrintf("timeout > McuWakeUpTime\r\ntimeout < 50\r\n");//调试用
            RtcTimerEventAllowsLowPower = false;
        }
        else
        {
				   	//DebugPrintf("timeout > 50ms 允许进入低功耗\r\n");//调试用
            RtcTimerEventAllowsLowPower = true;
            timeout -= McuWakeUpTime;
        }
    }
    return  timeout;
}

TimerTime_t RtcGetTimerValue( void )
{
    return( RtcConvertCalendarTickToTimerTime( NULL ) );
}

////获取最近一次闹钟中断唤醒之后，到目前时刻的时间戳值
TimerTime_t RtcGetElapsedAlarmTime( void )
{
    TimerTime_t currentTime = 0;
    TimerTime_t contextTime = 0;

		currentTime = RtcConvertCalendarTickToTimerTime( NULL );                //通过这个函数得到当前的时间 单位ms
    contextTime = RtcConvertCalendarTickToTimerTime( &RtcCalendarContext ); //RtcCalendarContext是记录最近一次闹钟唤醒时刻的脉冲数,也转换为ms
    //DebugPrintf("currentTime is %f \r\ncontextTime is %f \r\n",currentTime,contextTime);
    //计算两者的时间差值
    if( currentTime < contextTime ) //脉冲数溢出情况 当当前时间小于上一次的日历时间则是溢出了
    {
        return( currentTime + ( 0xFFFFFFFF - contextTime ) );
    }
    else                            //脉冲数未溢出情况
    {
        return( currentTime - contextTime ); //返回上一次中断到现在的时间ms
    }
}

TimerTime_t RtcComputeFutureEventTime( TimerTime_t futureEventInTime )
{
    return( RtcGetTimerValue( ) + futureEventInTime ); //将来事件的时间=当前时间+将来时间
}

TimerTime_t RtcComputeElapsedTime( TimerTime_t eventInTime )
{
    TimerTime_t elapsedTime = 0;

    // Needed at boot, cannot compute with 0 or elapsed time will be equal to current time
    if( eventInTime == 0 )
    {
        return 0;
    }

    elapsedTime = RtcConvertCalendarTickToTimerTime( NULL );        //获取当前时间戳

    //计算距离eventInTime的时间戳差值
    if( elapsedTime < eventInTime )     //时间戳溢出情况
    { // roll over of the counter   
        return( elapsedTime + ( 0xFFFFFFFF - eventInTime ) );
    }
    else           											 //时间戳未溢出情况
    {
        return( elapsedTime - eventInTime );
    }
}

void BlockLowPowerDuringTask ( bool status )
{
    if( status == true )
    {
        RtcRecoverMcuStatus( );
    }
		//DebugPrintf("执行中断并将标志置为false不允许系统进入低功耗");
    LowPowerDisableDuringTask = status;
}

void RtcEnterLowPowerStopMode( void )
{
    if( ( LowPowerDisableDuringTask == false ) && ( RtcTimerEventAllowsLowPower == true ) )
    {
        BoardDeInitMcu( );

        // Disable the Power Voltage Detector
       // HAL_PWR_DisablePVD( );
//#warning "Commented for debug!"

        SET_BIT( PWR->CR, PWR_CR_CWUF );

        // Enable Ultra low power mode
        //HAL_PWREx_EnableUltraLowPower( );

        // Enable the fast wake up from Ultra low power mode
       // HAL_PWREx_EnableFastWakeUp( );

        // Enter Stop Mode
			  DebugPrintf("进入HAL_PWR_EnterSTOPMode\r\n");
        HAL_PWR_EnterSTOPMode( PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI );
    }
}

void RtcRecoverMcuStatus( void )
{
    // PWR_FLAG_WU indicates the Alarm has waken-up the MCU
    if( __HAL_PWR_GET_FLAG( PWR_FLAG_WU ) != RESET )
    {
        __HAL_PWR_CLEAR_FLAG( PWR_FLAG_WU );
    }
    else
    {
        NonScheduledWakeUp = true;
    }
    // check the clk source and set to full speed if we are coming from sleep mode
    if( ( __HAL_RCC_GET_SYSCLK_SOURCE( ) == RCC_SYSCLKSOURCE_STATUS_HSI ) ||
        ( __HAL_RCC_GET_SYSCLK_SOURCE( ) == RCC_SYSCLKSOURCE_STATUS_PLLCLK ) )
    {
        BoardInitMcu( ); //这里会重新初始化一次,但是系统校准已经校准过了所以只会打印一次
    }
}

/******************************************************************************************
*****获取当前的时间与最近一个警报的时间进行计算并转化为ms放进McuWakeUpTime****************
*/
void RtcComputeWakeUpTime( void )
{
    uint32_t start = 0;
    uint32_t stop = 0;
    RTC_AlarmTypeDef  alarmRtc;
    RtcCalendar_t now;

    if( WakeUpTimeInitialized == false )
    {
        now = RtcGetCalendar( );
        HAL_RTC_GetAlarm( &RtcHandle, &alarmRtc, RTC_ALARM_A, RTC_FORMAT_BIN );

        start = alarmRtc.AlarmTime.Seconds + ( SecondsInMinute * alarmRtc.AlarmTime.Minutes ) + ( SecondsInHour * alarmRtc.AlarmTime.Hours );
        stop = now.CalendarTime.Seconds + ( SecondsInMinute * now.CalendarTime.Minutes ) + ( SecondsInHour * now.CalendarTime.Hours );
        //DebugPrintf("start is %f\r\n",start);
        //DebugPrintf("stop is %f\r\n",stop);			
        McuWakeUpTime = ceil ( ( stop - start ) * RTC_ALARM_TICK_DURATION );
        //DebugPrintf("McuWakeUpTime is %d\r\n",McuWakeUpTime);
        WakeUpTimeInitialized = true;
    }
}

static void RtcStartWakeUpAlarm( uint32_t timeoutValue )
{//传入的timeoutValue是超时的时间值1000
    RtcCalendar_t now;
    RtcCalendar_t alarmTimer;
    RTC_AlarmTypeDef alarmStructure;

//    HAL_RTC_DeactivateAlarm( &RtcHandle, RTC_ALARM_A );
//    
//    // Clear RTC Alarm Flag
//    __HAL_RTC_ALARM_CLEAR_FLAG( &RtcHandle, RTC_FLAG_ALRAF );

//    // Clear the EXTI's line Flag for RTC Alarm
//    __HAL_RTC_ALARM_EXTI_CLEAR_FLAG( );

	  HAL_RTC_DeactivateAlarm( &RtcHandle, RTC_ALARM_A );
//    HAL_RTCEx_DeactivateWakeUpTimer( &RtcHandle ); //151有这个
	
    // Load the RTC calendar
    now = RtcGetCalendar( );

    // Save the calendar into RtcCalendarContext to be able to calculate the elapsed time
    RtcCalendarContext = now;

    // timeoutValue is in ms
    alarmTimer = RtcComputeTimerTimeToAlarmTick( timeoutValue, now );
    //RtcComputeTimerTimeToAlarmTick计算了now日历到timeoutValue的时间   并将结果转换为日历形式返回到alarmTimer
		alarmStructure.Alarm = RTC_ALARM_A;
    alarmStructure.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;  //选择日期为闹钟参数
    alarmStructure.AlarmTime.TimeFormat = RTC_HOURFORMAT12_AM;
	  alarmStructure.AlarmMask = RTC_ALARMMASK_NONE;
//		alarmStructure.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
//    alarmStructure.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
		   

    alarmStructure.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL; //屏蔽亚秒
		

	  //将得到的日历作为Alarm A的闹钟设置
		alarmStructure.AlarmTime.SubSeconds = 0;  //未计算亚秒
    alarmStructure.AlarmTime.Seconds = alarmTimer.CalendarTime.Seconds;
    alarmStructure.AlarmTime.Minutes = alarmTimer.CalendarTime.Minutes;
    alarmStructure.AlarmTime.Hours = alarmTimer.CalendarTime.Hours;
		alarmStructure.AlarmDateWeekDay = alarmTimer.CalendarDate.Date;

    //DebugPrintf("SET HAL_RTC_SetAlarm_IT\r\n");
    //开闹钟
    HAL_RTC_SetAlarm_IT( &RtcHandle, &alarmStructure, RTC_FORMAT_BIN );
}
//计算now(日历)到目标时间timeoutValue的时间戳,把结果转换为日历返回
static RtcCalendar_t RtcComputeTimerTimeToAlarmTick( TimerTime_t timeCounter, RtcCalendar_t now )
{
    RtcCalendar_t calendar = now;

    uint16_t seconds = now.CalendarTime.Seconds;
    uint16_t minutes = now.CalendarTime.Minutes;
    uint16_t hours = now.CalendarTime.Hours;
    uint16_t days = now.CalendarDate.Date;
    double timeoutValueTemp = 0.0;
    double timeoutValue = 0.0;
    double error = 0.0;

    timeCounter = MIN( timeCounter, ( TimerTime_t )( RTC_ALARM_MAX_NUMBER_OF_DAYS * SecondsInDay * RTC_ALARM_TICK_DURATION ) );

    if( timeCounter < 1 )
    {
        timeCounter = 1;
    }

    // timeoutValue is used for complete computation
    timeoutValue = round( timeCounter * RTC_ALARM_TICK_PER_MS );  //将时间戳转换为真正的时间ms秒,并四舍五入

    // timeoutValueTemp is used to compensate the cumulating errors in timing far in the future
		//timeoutValueTemp用于补偿将来远期的累积误差
    timeoutValueTemp =  ( double )timeCounter * RTC_ALARM_TICK_PER_MS;

    // Compute timeoutValue error
    error = timeoutValue - timeoutValueTemp;

    // Add new error value to the cumulated value in uS
    TimeoutValueError += ( error  * 1000 );

    // Correct cumulated error if greater than ( RTC_ALARM_TICK_DURATION * 1000 )
		//如果大于（RTC_ALARM_TICK_DURATION * 1000），则纠正累积误差
    if( TimeoutValueError >= ( int32_t )( RTC_ALARM_TICK_DURATION * 1000 ) )
    {
        TimeoutValueError = TimeoutValueError - ( uint32_t )( RTC_ALARM_TICK_DURATION * 1000 );
        timeoutValue = timeoutValue + 1;
    }

    // Convert milliseconds to RTC format and add to now
		//将毫秒转换为RTC日历格式并添加到now
    while( timeoutValue >= SecondsInDay )
    {
        timeoutValue -= SecondsInDay;
        days++;
    }

    // Calculate hours
    while( timeoutValue >= SecondsInHour )
    {
        timeoutValue -= SecondsInHour;
        hours++;
    }

    // Calculate minutes
    while( timeoutValue >= SecondsInMinute )
    {
        timeoutValue -= SecondsInMinute;
        minutes++;
    }

    // Calculate seconds
    seconds += timeoutValue;

    // Correct for modulo
    while( seconds >= 60 )
    {
        seconds -= 60;
        minutes++;
    }

    while( minutes >= 60 )
    {
        minutes -= 60;
        hours++;
    }

    while( hours >= HoursInDay )  //hours等于24小时Day++
    {
        hours -= HoursInDay;
        days++;
    }

    if( ( now.CalendarDate.Year == 0 ) || ( ( now.CalendarDate.Year + Century ) % 4 ) == 0 )
    {
        if( days > DaysInMonthLeapYear[now.CalendarDate.Month - 1] )
        {
            days = days % DaysInMonthLeapYear[now.CalendarDate.Month - 1];
        }
    }
    else
    {
        if( days > DaysInMonth[now.CalendarDate.Month - 1] )
        {
            days = days % DaysInMonth[now.CalendarDate.Month - 1];
        }
    }

    calendar.CalendarTime.Seconds = seconds;
    calendar.CalendarTime.Minutes = minutes;
    calendar.CalendarTime.Hours = hours;
    calendar.CalendarDate.Date = days;

    return calendar;
}

//
// REMARK: Removed function static attribute in order to suppress
//         "#177-D function was declared but never referenced" warning.
// static RtcCalendar_t RtcConvertTimerTimeToCalendarTick( TimerTime_t timeCounter )


////将时间戳转换成内部RTC时钟脉冲数，即 时间戳-->RTC脉冲数
RtcCalendar_t RtcConvertTimerTimeToCalendarTick( TimerTime_t timeCounter )
{
    RtcCalendar_t calendar = { 0 };

    uint16_t seconds = 0;
    uint16_t minutes = 0;
    uint16_t hours = 0;
    uint16_t days = 0;
    uint8_t months = 1; // Start at 1, month 0 does not exist
    uint16_t years = 0;
    uint16_t century = 0;
    double timeCounterTemp = 0.0;

    timeCounterTemp = ( double )timeCounter * RTC_ALARM_TICK_PER_MS;  //将时间戳转成脉冲数

    // Convert milliseconds to RTC format and add to now
		 // 将脉冲数转成RTC格式
    while( timeCounterTemp >= SecondsInLeapYear )
    {
        if( ( years == 0 ) || ( years % 4 ) == 0 )
        {
            timeCounterTemp -= SecondsInLeapYear;
        }
        else
        {
            timeCounterTemp -= SecondsInYear;
        }
        years++;
        if( years == 100 )
        {
            century = century + 100;
            years = 0;
        }
    }

    if( timeCounterTemp >= SecondsInYear )
    {
        if( ( years == 0 ) || ( years % 4 ) == 0 )
        {
            // Nothing to be done
        }
        else
        {
            timeCounterTemp -= SecondsInYear;
            years++;
        }
    }

    if( ( years == 0 ) || ( years % 4 ) == 0 )
    {
        while( timeCounterTemp >= ( DaysInMonthLeapYear[ months - 1 ] * SecondsInDay ) )
        {
            timeCounterTemp -= DaysInMonthLeapYear[ months - 1 ] * SecondsInDay;
            months++;
        }
    }
    else
    {
        while( timeCounterTemp >= ( DaysInMonth[ months - 1 ] * SecondsInDay ) )
        {
            timeCounterTemp -= DaysInMonth[ months - 1 ] * SecondsInDay;
            months++;
        }
    }

    // Convert milliseconds to RTC format and add to now
    while( timeCounterTemp >= SecondsInDay )
    {
        timeCounterTemp -= SecondsInDay;
        days++;
    }

    // Calculate hours
    while( timeCounterTemp >= SecondsInHour )
    {
        timeCounterTemp -= SecondsInHour;
        hours++;
    }

    // Calculate minutes
    while( timeCounterTemp >= SecondsInMinute )
    {
        timeCounterTemp -= SecondsInMinute;
        minutes++;
    }

    // Calculate seconds
    seconds = round( timeCounterTemp );

    calendar.CalendarTime.Seconds = seconds;
    calendar.CalendarTime.Minutes = minutes;
    calendar.CalendarTime.Hours = hours;
    calendar.CalendarDate.Date = days;
    calendar.CalendarDate.Month = months;
    calendar.CalendarDate.Year = years;
    calendar.CalendarCentury = century;

    return calendar;
}
//若没有参数传入时则为获取当前时间日历,并将日历转换为-->时间ms
//有参数则为将传入的日历转换为时间ms
static TimerTime_t RtcConvertCalendarTickToTimerTime( RtcCalendar_t *calendar )
{
    TimerTime_t timeCounter = 0;
    RtcCalendar_t now;
    double timeCounterTemp = 0.0;

    // Passing a NULL pointer will compute from "now" else,
    // compute from the given calendar value
    if( calendar == NULL )
    {
        now = RtcGetCalendar( );  //获取当前日历进行计算
    }
    else
    {
        now = *calendar;          //从给定的日历开始计算
    }

    // Years (calculation valid up to year 2099)
    for( int16_t i = 0; i < ( now.CalendarDate.Year + now.CalendarCentury ); i++ )   //年数转换成时钟脉冲数
    {
        if( ( i == 0 ) || ( i % 4 ) == 0 )
        {
            timeCounterTemp += ( double )SecondsInLeapYear;
        }
        else
        {
            timeCounterTemp += ( double )SecondsInYear;
        }
    }

    // Months (calculation valid up to year 2099)*/
    if( ( now.CalendarDate.Year == 0 ) || ( ( now.CalendarDate.Year + now.CalendarCentury ) % 4 ) == 0 )  //月数转换成时钟脉冲数
    {
        for( uint8_t i = 0; i < ( now.CalendarDate.Month - 1 ); i++ )
        {
            timeCounterTemp += ( double )( DaysInMonthLeapYear[i] * SecondsInDay );
        }
    }
    else
    {
        for( uint8_t i = 0;  i < ( now.CalendarDate.Month - 1 ); i++ )
        {
            timeCounterTemp += ( double )( DaysInMonth[i] * SecondsInDay );
        }
    }
//时分秒日转换成秒数
    timeCounterTemp += ( double )( ( uint32_t )now.CalendarTime.Seconds +
                     ( ( uint32_t )now.CalendarTime.Minutes * SecondsInMinute ) +
                     ( ( uint32_t )now.CalendarTime.Hours * SecondsInHour ) +
                     ( ( uint32_t )( now.CalendarDate.Date * SecondsInDay ) ) );

    timeCounterTemp = ( double )timeCounterTemp * RTC_ALARM_TICK_DURATION;  //将得到的RTC时钟脉冲数转换成时间，单位ms

    timeCounter = round( timeCounterTemp );//时间ms四舍五入求值
    return ( timeCounter );
}

static void RtcCheckCalendarRollOver( uint8_t year )
{
    if( year == 99 )
    {
        CalendarRollOverReady = true;
    }

    if( ( CalendarRollOverReady == true ) && ( ( year + Century ) == Century ) )
    {   // Indicate a roll-over of the calendar
        CalendarRollOverReady = false;
        Century = Century + 100;
    }
}

//从RTC里获取日历
static RtcCalendar_t RtcGetCalendar( void )
{

    RtcCalendar_t calendar;
	
    HAL_RTC_GetTime( &RtcHandle, &calendar.CalendarTime, RTC_FORMAT_BIN );
    HAL_RTC_GetDate( &RtcHandle, &calendar.CalendarDate, RTC_FORMAT_BIN );
	  //DebugPrintf("CalendarTime Minutes is %f\r\n",calendar.CalendarTime.Minutes);
		//DebugPrintf("CalendarTime Seconds is %f\r\n",calendar.CalendarTime.Seconds);

    calendar.CalendarCentury = Century;
    RtcCheckCalendarRollOver( calendar.CalendarDate.Year );
    return calendar;
}

/*!
 * \brief RTC IRQ Handler of the RTC Alarm
 */
//void RTC_Alarm_IRQHandler( void )
//{
//		DebugPrintf("rtc-board.c\r\n");
//    HAL_RTC_AlarmIRQHandler( &RtcHandle );                  ///闹钟中断处理函数  清除挂起位 清除外部中断标志位
//    HAL_RTC_DeactivateAlarm( &RtcHandle, RTC_ALARM_A );			//失能闹钟
//    RtcRecoverMcuStatus( );                                 //从休眠中唤醒MCU，进行状态恢复
//    RtcComputeWakeUpTime( );                                //计算唤醒保持时间
//    BlockLowPowerDuringTask( false );                       //阻塞进入低功耗
//    TimerIrqHandler( );                                     //RTC定时器中断处理，处理定时器链表
//}
//void HAL_RTC_AlarmAEventCallback( RTC_HandleTypeDef *hrtc )
//{
//    TimerIrqHandler( );
//}
