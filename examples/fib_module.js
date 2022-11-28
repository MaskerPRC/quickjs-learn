// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  光纤模块 */ 
export function fib(n)
{
    if (n <= 0)
        return 0;
    else if (n == 1)
        return 1;
    else
        return fib(n - 1) + fib(n - 2);
}
