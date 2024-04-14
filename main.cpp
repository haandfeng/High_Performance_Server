#include <iostream>
#include <algorithm>
#include <cmath>

using namespace std;

// prototype   实质上三个函数的参数列表是等价的
const double* f1(const double arr[], int n);
const double* f2(const double [], int);
const double* f3(const double* , int);
const double* f4(const double* );
const int f5(int n);

int main(int argc, char *argv[])
{
    double a[3] = {12.1, 3.4, 4.5};

    // 声明指针
    const double* (*p1)(const double*, int) = f1;
    const double* (*p2)(const double*, int) = f2;
    const double* (*p3)(const double*, int) = f3;
    const double* (*p4)(const double*) = f4;
    const int (*p5)(int) = f5;

    cout << "Pointer 1 : " << p1 << " : " << (p1(a, 3)) << endl;
    cout << "Pointer 1 : " << (*p1)<< " : " << *((*p1)(a, 3)) << endl;
    cout << "Pointer 2 : " << p2 << " : " << (p2(a, 3)) << endl;
    cout << "Pointer 2 : " << (*p2)<< " : " << *((*p2)(a, 3)) << endl;
    cout << "Pointer 3 : " << p3 << " : " << (p3(a, 3)) << endl;
    cout << "Pointer 3 : " << (*p3)<< " : " << *((*p3)(a, 3)) << endl;
    cout << "Pointer 4 : " << p4 << " : " << (p4(a)) << endl;
    cout << "Pointer 4 : " << (*p4)<< " : " << *((*p4)(a)) << endl;
    cout << "Pointer 5 : " << p5 << " : " << (p5(12)) << endl;
    cout << "Pointer 5 : " << (*p5)<< " : " <<((*p5)(12)) << endl;
    const double* (*parray[3])(const double *, int) = {f1, f2, f3};   // 声明一个指针数组，存储三个函数的地址
    cout << "Pointer array : " << parray[0] << " : " << *(parray[0](a, 3)) << endl;
    cout << "Pointer array : " << parray[1]<< " : " << *(parray[1](a, 3)) << endl;
    cout << "Pointer array : " << *parray[2] << " : " << *((*parray[2])(a, 3)) << endl;

    return 0;
}


const double* f1(const double arr[], int n)
{
    return arr;     // 首地址
}

const double* f2(const double arr[], int n)
{
    return arr+1;
}

const double* f3(const double* arr, int n)
{
    return arr+2;
}

const double* f4(const double* arr)
{
    return arr+2;
}
const int f5( int n)
{
    return n;
}