#include "ch32fun.h"
#include <stdio.h>

int main()
{
    SystemInit();
    
    // Enable GPIOD
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD;

    // PD6 push-pull output
    GPIOD->CFGLR &= ~(0xf<<(4*6));
    GPIOD->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*6);

    while(1)
    {
        GPIOD->OUTDR ^= 1<<6;	// Toggle PD6
        Delay_Ms(1000);
    }
}