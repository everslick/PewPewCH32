#include "ch32fun.h"
#include <stdio.h>

int main()
{
    SystemInit();
    
    // Enable GPIOC
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC;
    
    // PC1 push-pull output
    GPIOC->CFGLR &= ~(0xf<<(4*1));
    GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*1);
    
    while(1)
    {
        GPIOC->OUTDR ^= 1<<1;	// Toggle PC1
        Delay_Ms(1000);
    }
}