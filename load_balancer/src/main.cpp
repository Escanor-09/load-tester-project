#include "LoadBalancer.h"

int main()
{
    LoadBalancer lb(8080);
    lb.start();
}