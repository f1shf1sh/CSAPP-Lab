#include <stdio.h>

int main(){
	int oddSign = 0xaa+(0xaa<<8);
	oddSign = oddSign+(oddSign<<16);
	printf("%x\n",oddSign);
}
