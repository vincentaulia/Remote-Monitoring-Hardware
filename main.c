

/* Vincent Aulia
 * February 9 2017 - initial version
 * July 4 2017 - first version to successfully send temp data to digi remote manager
 * August 16 2017 - first version to implement lpm3, switch from SMCLK to ACLK to save power
 * August 23 2017 - xbee is now using hibernate mode, MUST disable xbee pin DIO7/CTS in XCTU, @32khz wake/transmit time is ~25ms
 * September 7 2017 - added delay based on counting # of sleep cycles. counter variable is delay & delayLimit
 *                  delayLimit determines length of time to keep sleeping. Controllable by P2.0 and P2.1 per HW revision 0.2.
 * main.c
 */

#include <msp430.h>

int main(void)
{
    //Initializing Watchdog Control Delay, Interrupts
  //BCSCTL1 |= DIVA_2;                        // ACLK/4
  WDTCTL = WDT_ADLY_1000;                   // WDT 1s/4 interval timer
  IE1 |= WDTIE;                             // Enable WDT interrupt
  IE2 |= UCA0TXIE + UCA0RXIE + UCB0TXIE + UCB0RXIE; //enable TX and RX interrupt
//  P1DIR = 0xFF;                             // All P1.x outputs
//  P1OUT = 0;                                // All P1.x reset
//  P2DIR = 0xFF;                             // All P2.x outputs
//  P2OUT = 0;                                // All P2.x reset

      //
      char thermoData[4];   //32-bit data straight from the MAX31855 thermocouple IC
      char thermoProbe[4];  //32-bit data = 4 chars converted from thermoData
      int i, j;             //integers for looping
      long timer;           //long integer for timers
      int delay=0;          //count # of sleep cycles
      int delayLimit = 0;   //sleep cycle limit determined by pin status

      //ThermoData bits
      int thermoProbeRead; //12-bit (converted to 16-bit int) thermocouple temperature probe read [bit 31:18]
      //below not used for now
      int thermoInternal; //12-bit (converted to 16-bit int) thermocouple internal temperature data [bit 15:4]
      char thermoSCVFault = 0; //reads 1 whenever thermocouple is being short-circuited to VCC. Default is 0 [bit 2]
      char thermoSCGFault = 0; //reads 1 whenever thermocouple is being short-circuited to GND. Default is 0 [bit 1]
      char thermoOCFault = 0; //reads 1 whenever thermocouple is open (no connections). Default is 0 [bit 0]
      char thermoFault = 0; //reads 1 whenever SCV, SCG, OC faults are active. Default is 0 [bit 16]

      //UART A0 setup
      UCA0CTL1 = UCSWRST;   //resets ucontroller, enables UCAxCTL0/1 register read/write
      UCA0CTL0 = 0;         //No parity, LSB first, 8-bit, 1 stop bit, UART mode
      UCA0CTL1 |= UCSSEL0;  //use ACLK
      UCA0BR0 = 3;          //32khz 9600 baud  112; //1.0476Mhz 9600 baud
      UCA0BR1 = 0;
      UCA0MCTL |= UCBRS0 + UCBRS1; // Modulation UCBRSx = 3

      //SPI B0 setup
      UCB0CTL1 = UCSWRST;   //resets ucontroller, enables UCBxCTL0/1 register read/write
      UCB0CTL0 |= UCCKPL + UCMSB + UCMST; //0 clk phase & polarity, LSB first, 8-bit, Master mode, 3-pin SPI
      UCB0CTL1 = UCSSEL1;   //SMCLK
      UCB0BR0 = 0;          //1.0476Mhz bit rate without prescaler
      UCB0BR1 = 0;

      //port setup
      //BITx = P1.x, read Texas Instrument MSP430G2553 documentation for port function selector values.
      P1DIR |= BIT0 + BIT3 + BIT4 + BIT2 + BIT7; //P1 ports are output ports
      P1SEL |= BIT0 + BIT1 + !BIT3 + BIT2 + BIT4 + BIT5 + BIT6 + !BIT7;
      P1SEL2 |= BIT1 + BIT2 + !BIT3 + BIT5 + BIT6 + !BIT7;

      P2DIR |= !BIT0 + !BIT0; //P2.0 and 2.1 are input pins
      P2SEL |= !BIT0 + !BIT0;
      P2SEL2 |= !BIT0 + !BIT0;

      //finish setup
      UCA0CTL1 &= ~UCSWRST;
      UCB0CTL1 &= ~UCSWRST;
//    P1OUT = BIT3 + BIT7; //turn off thermometer and xbee to make sure both always start as off
  while(1)
  {
//    int i;
//    P1OUT |= 0x01;                          // Set P1.0 LED on
//    for (i = 5000; i>0; i--);               // Delay
//    P1OUT &= ~0x01;                         // Reset P1.0 LED off

      //wait for read request from DIGI cloud server
              //add later

              P1OUT = !BIT3 + !BIT7;    //drive xbee pin 9 and SPI slave select low to activate both from non-operation.
              //check status of delayLimit pins
              if (P2IN && 0x03) { //P2.0 & P2.1 high
                  delayLimit = 10;
              } else if (P2IN && 0x01) { //P2.0 high
                  delayLimit = 20;
              } else if (P2IN && 0x02) { //P2.1 high
                  delayLimit = 30;
              } else delayLimit = 40;  //none high

              //check delay
             if (delay == 50) { //if delay is 50 * 4s, turn everything on



                  //for loop to get thermoData
                  for (i=0; i<4; i++){
                      IFG2 &= ~UCB0RXIFG;           //clear pending interrupt flag
                      UCB0TXBUF = 0x00;             //send dummy to SPI TX buffer
                      while(!(IFG1&UCB0RXIFG));     //wait for global interrupt flag and SPI RX interrupt flag
                      thermoData[i] = UCB0RXBUF;    //get SPI RX thermocouple data, place into char array
                  }

                  //pull 14-bit thermocouple temperature read data to 16-bit int
                  //first, check if temp is +ve or -ve
                  thermoProbeRead = 0;
                  if (thermoData[0] & (1 << 8)) {//check msb of thermoData[0] for 2's complement
                      thermoProbeRead = (0xFF00 << 8) | (thermoData[0] << 4) | (thermoData[1] >> 4); //if -ve
                      //then convert thermoProbeRead int into 4-char string thermoProbe
                      int b = 3;
                      for (b; b>1; b--) {
                      thermoProbe[b] = thermoProbeRead % 10 + '0'; thermoProbeRead /= 10; }
                      thermoProbe[1] = thermoProbeRead % 10 + '0';
                      thermoProbe[0] = '-';}
                  else {
                      thermoProbeRead = (0x0000 << 8) | (thermoData[0] << 4) | (thermoData[1] >> 4); //if +ve
                      //then convert thermoProbeRead int into 4-char string thermoProbe
                      int b = 3;
                      for (b; b>0; b--) {
                      thermoProbe[b] = thermoProbeRead % 10 + '0'; thermoProbeRead /= 10; }
                      thermoProbe[0] = thermoProbeRead % 10 + '0'; }

                  //for loop to send thermometer read to cloud
                 for(j=0;j<4;j++) {
                     //IFG1 &= ~UCA0TXIFG; //clear pending interrupt flag
                     while(!(IFG2&UCA0TXIFG));      //wait for global interrupt flag and UART TX interrupt flag
                     UCA0TXBUF = thermoProbe[j];    //send thermoProbe chars individually

                     timer = 0;
                     do {timer++;} while (timer<10); //small delay before sending another char
                     }

                  timer = 0;
                  do {timer++;} while (timer<1000); //~1s delay before sending another data point

                  delay = 0;
              }
             else delay++;

              P1OUT = BIT3 + BIT7; //turn off thermometer and xbee to make sure both always start as off

    __bis_SR_register(LPM3_bits + GIE);     // Enter LPM3
  }
}

#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=WDT_VECTOR
__interrupt void watchdog_timer (void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(WDT_VECTOR))) watchdog_timer (void)
#else
#error Compiler not supported!
#endif
{
    __bic_SR_register_on_exit(LPM3_bits);   // Clear LPM3 bits from 0(SR)
}


