// TestSuiteSparse.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "stdio.h"
#include <iostream>
#include <string>
#include <cmath>
#include "suitesparse_utilities.h"
#include "linear_system.h"

void hard_coded_test();
void read_system_test();
bool check_same(int length, double* array1, double* array2, double tolerance);
void print_array(int length, double* arry);

int main()
{
    //hard_coded_test();
	read_system_test();
    return 0;
}

void read_system_test()
{
    std::string csc_values_path = "C:\\Users\\Serafeim\\Desktop\\GRACM\\matrixCSC-values.txt";
    std::string csc_rows_path = "C:\\Users\\Serafeim\\Desktop\\GRACM\\matrixCSC-row indices.txt";
    std::string csc_cols_path = "C:\\Users\\Serafeim\\Desktop\\GRACM\\matrixCSC-column offsets.txt";
    std::string rhs_path = "C:\\Users\\Serafeim\\Desktop\\GRACM\\rhs.txt";
    std::string solution_path = "C:\\Users\\Serafeim\\Desktop\\GRACM\\solution.txt";

	int values_length, rows_length, cols_length, rhs_length, solution_length;
	double* values = read_double_array(csc_values_path, &values_length);
	int* row_indices = read_int_array(csc_rows_path, &rows_length);
	int* col_offsets = read_int_array(csc_cols_path, &cols_length);
	double* rhs = read_double_array(rhs_path, &rhs_length);
	double* solution_expected = read_double_array(solution_path, &solution_length);
	int nnz = values_length;
	int order = cols_length - 1;
	double* solution = new double[order];

	//TODO: check that dimensions match
	
	//Use the library to solve the system
	cholmod_common *common = util_create_common(0, 0);
	cholmod_factor *factor = NULL;
	util_factorize_cscupper(order, nnz, values, row_indices, col_offsets, &factor, common);
	util_solve(order, factor, rhs, solution, common);
	util_destroy_factor(&factor, common);
	util_destroy_common(&common);

	//Check solution
	if (check_same(order, solution, solution_expected, 1e-6)) std::cout << "The linear system has been solved correctly." << '\n';
	else std::cout << "ERROR in solving the linear system." << '\n';
	std::cout << '\n' << "expected solution = ";
	print_array(order, solution_expected);
	std::cout << '\n' << "computed solution = ";
	print_array(order, solution);

	//Clean up
	delete[] col_offsets;
	delete[] row_indices;
	delete[] values;
	delete[] rhs;
	delete[] solution;
	delete[] solution_expected;
}

void hard_coded_test()
{
    // Define linear system
    int n = 4;
    int nnz = 7;
    int *col_offsets = new int[n + 1]{ 0, 1, 2, 5, nnz };
    int *row_indices = new int[nnz]{ 0, 1, 0, 1, 2, 1, 3 };
    double *values = new double[nnz]{ 4.0, 10.0, 2.0, 1.0, 8.0, 3.0, 9.0 };
    double *b = new double[n]{ 6.0, 14.0, 11.0, 12.0 };
    double *x = new double[n];

    //Use the library to solve the system
    cholmod_common *common = util_create_common(0, 0);
    cholmod_factor *factor = NULL;
    util_factorize_cscupper(n, nnz, values, row_indices, col_offsets, &factor, common);
    util_solve(n, factor, b, x, common);
    util_destroy_factor(&factor, common);
    util_destroy_common(&common);

    //Check solution
    double *expected = new double[4] { 1.0, 1.0, 1.0, 1.0 };
	if (check_same(n, x, expected, 1e-6)) std::cout << "The linear system has been solved correctly." << '\n';
	else std::cout << "ERROR in solving the linear system." << '\n';
	std::cout << '\n' << "expected solution = ";
	print_array(n, expected);
	std::cout << '\n' << "computed solution = ";
	print_array(n, x);

    //Clean up
    delete[] col_offsets;
    delete[] row_indices;
    delete[] values;
    delete[] b;
    delete[] x;
    delete[] expected;
}

bool check_same(int length, double* array1, double* array2, double tolerance)
{
    for (int i = 0; i < length; ++i)
    {
        if (abs(array1[i] - array2[i]) > tolerance) return false;
    }
    return true;
}

void print_array(int length, double* arry)
{
    for (int i = 0; i < length; ++i)
    {
        printf("%f", arry[i]);
        printf(" ");
    }
    printf("\n");
}