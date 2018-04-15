// suitesparse_utilities.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include <string.h>
#include <algorithm>
#include "cholmod.h"
#include "suitesparse_utilities.h"

/*
 * Allocates in heap and returns a handle that must be passed to all CHOLMOD functions.
 */
cholmod_common* util_create_common()
{
	cholmod_common *common = new cholmod_common;
	cholmod_start(common);
	return common;
}

/*
 * Frees the memory for the handle.
 */
void util_destroy_common(cholmod_common** common)
{
	cholmod_finish(*common);
	*common = NULL;
}

/*
 * Frees the memory for the factorized matrix.
 */
void util_destroy_factor(cholmod_factor** factorized_matrix, cholmod_common* common)
{
	cholmod_free_factor(factorized_matrix, common);
	*factorized_matrix = NULL;
}

/*
 * Factorize a symmetric matrix using cholesky algorithm. The matrix is in csc form, with only the upper triangle stored.
 * If cholesky is successful -1 is returned and out_factorized_matrix points to the factorized upper triangle.
 * If the matrix is not positive definite then the index (0-based) of the column where cholesky failed
 * and out_factorized_matrix = NULL.
 * If the something another failure occurs, such as memory not being sufficient due to excessive fill-in then -2 is returned
 * and out_factorized_matrix = NULL.
 */
int util_factorize_cscupper(
	int order, /*Number of rows = number of columns*/
	int nnz, /*Number of non zero entries in the upper triangle.*/
	double* values, /*Array containing the non zero entries of the upper triangle in column major order. Length = nnz*/
	int* row_indices, /*Array containing the row indices of the non zero entries of the upper triangle. Length = nnz*/
	int*  col_offsets, /*Array containing the indices into values (and row_indices) of the first entry of each column. Length = order + 1. The last entry is col_offsets[order] = nnz*/
	cholmod_factor** out_factorized_matrix, /*Out parameter: the factorized upper triangle of the symmetric CSC matrix.*/
	cholmod_common* common)
{
	// Create temp sparse matrix object
	cholmod_sparse *A = (cholmod_sparse*)cholmod_malloc(1, sizeof(cholmod_sparse), common);
	A->nrow = order;
	A->ncol = order;
	A->nzmax = nnz;
	A->p = col_offsets;
	A->nz = NULL; // Column indices if the matrix is unpacked.
	A->i = row_indices;
	A->x = values;
	A->z = NULL; //imaginary parts if the matrix is complex
	A->stype = 1; // >0: upper triangle is stored, <0: lower triangle is stored, 0: unsymmetric matrix
	A->itype = CHOLMOD_INT; //type of the void index arrays (p,i,nz). Options: CHOLMOD_INT, CHOLMOD_INTLONG, CHOLMOD_LONG
	A->xtype = CHOLMOD_REAL; //type of the value arrays (x, z). Options: CHOLMOD_PATTERN, CHOLMOD_REAL, CHOLMOD_COMPLEX, CHPLMOD_ZOMPLEX
	A->dtype = CHOLMOD_DOUBLE; //precision of value arrays. Options: CHOLMOD_REAL, CHOLMOD_FLOAT
	A->sorted = 1; // TRUE (1) if the columns are sorted, FALSE(0) otherwise
	A->packed = 1; // FALSE (0) if the column indexes are stored explicitly in nz, TRUE(1) otherwise (nz = NULL)

	// Factorize
	cholmod_factor *L = cholmod_analyze(A, common);
	int status = cholmod_factorize(A, L, common);

	// Free temp sparse matrix object. 
	A->p = NULL; //The CSC arrays belong to the caller and should not be freed by cholmod_free_sparse().
	A->i = NULL;
	A->x = NULL;
	cholmod_free_sparse(&A, common);

	// Inspect the result of the factorization
	if (status == 0) //FALSE=0 (not sure)
	{
		cholmod_free_factor(&L, common);
		out_factorized_matrix = NULL;
		return -2;
	}
	else if (common->status == CHOLMOD_NOT_POSDEF) //In this case cholmod_factorize() returns TRUE 
	{
		cholmod_free_factor(&L, common);
		out_factorized_matrix = NULL;
		return L->minor;
	}
	else
	{
		*out_factorized_matrix = L;
		return -1;
	}
}

/*
 * Adds a row and column to an LDL' factorization. Before the update, the kth row and column of L	must be equal to the kth
 * row and column of the identity matrix. The row/column to add must be a sparse CSC matrix with dimensions n-by-1,
 * where n is the order of the matrix.	Returns 1 if the method succeeds, 0 otherwise.
 */
int util_row_add(
	int order, /*Number of rows = number of columns*/
	cholmod_factor* L, /*The LDL' factorization of the matrix. It will be modified.*/
	int k, /*Index of row/column to add*/
	int vector_nnz, /*Number of non zero entries of the row/column to add.*/
	double* vector_values, /*The CSC format values of the row/column to add.*/
	int* vector_row_indices, /*The CSC format row indices of the row/column to add.*/
	int* vector_col_offsets, /*The CSC format column offsets of the row/column to add.*/
	cholmod_common* common)
{
	// Create temp sparse vector object
	cholmod_sparse *vector = (cholmod_sparse*)cholmod_malloc(1, sizeof(cholmod_sparse), common);
	vector->nrow = order;
	vector->ncol = 1;
	vector->nzmax = vector_nnz;
	vector->p = vector_col_offsets;
	vector->nz = NULL; // Column indices if the matrix is unpacked.
	vector->i = vector_row_indices;
	vector->x = vector_values;
	vector->z = NULL; //imaginary parts if the matrix is complex
	vector->stype = 0; // >0: upper triangle is stored, <0: lower triangle is stored, 0: unsymmetric matrix
	vector->itype = CHOLMOD_INT; //type of the void index arrays (p,i,nz). Options: CHOLMOD_INT, CHOLMOD_INTLONG, CHOLMOD_LONG
	vector->xtype = CHOLMOD_REAL; //type of the value arrays (x, z). Options: CHOLMOD_PATTERN, CHOLMOD_REAL, CHOLMOD_COMPLEX, CHPLMOD_ZOMPLEX
	vector->dtype = CHOLMOD_DOUBLE; //precision of value arrays. Options: CHOLMOD_REAL, CHOLMOD_FLOAT
	vector->sorted = 1; // TRUE (1) if the columns are sorted, FALSE(0) otherwise
	vector->packed = 1; // FALSE (0) if the column indexes are stored explicitly in nz, TRUE(1) otherwise (nz = NULL)

	// Update the LDL' factorization
	int status = cholmod_rowadd(k, vector, L, common);

	// Free temp sparse vector object. 
	vector->p = NULL; //The CSC arrays belong to the caller and should not be freed by cholmod_free_sparse().
	vector->i = NULL;
	vector->x = NULL;
	cholmod_free_sparse(&vector, common);

	// Inspect the result of the update
	if (status == 1) // TRUE, I think
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

/*
 * Deletes a row and column from an LDL' factorization. After updating the kth row and column of L will be equal to the kth
 * row and column of the identity matrix. Returns 1 if the method succeeds, 0 otherwise.
 */
int util_row_delete(
	cholmod_factor* L, /*The LDL' factorization of the matrix. It will be modified.*/
	int k, /*Index of row/column to add*/
	cholmod_common* common)
{
	// Update the LDL' factorization. 
	int status = cholmod_rowdel(k, NULL, L, common);

	//Check the result of the update
	if (status == 1) // TRUE, I think
	{
		return 1;
	}
	else
	{
		return 0;
	}

	// TODO: There is an option to provide the sparsity pattern of the row to delete, in order to reduce calculation time.
	// However, it would have to be computed by the caller which might actually be slower. Investigate the performance gain, if 
	// any, and whether it is worth the extra code complexity.
}

/*
 * Solves a linear system with a single right hand side vector.
 */
void util_solve(
	int order /*Number of matrix rows = number of matrix columns = length of right hand side vector*/,
	cholmod_factor* factorized_matrix, /*The LDL' factorization of the matrix.*/
	double* rhs, /*The right hand side vector. Its length must be equal to the order of the matrix: factorized_matrix->n*/
	double* out_solution, /*Buffer for the left hand side vector (unknown). Its length must be equal to the order of the matrix: factorized_matrix->n*/
	cholmod_common* common)
{
	// Create temp dense matrix object
	cholmod_dense *b = (cholmod_dense*)cholmod_malloc(1, sizeof(cholmod_dense), common);
	b->nrow = order;
	b->ncol = order;
	b->nzmax = order;
	b->d = order;
	b->x = rhs;
	b->z = NULL; //imaginary parts if the matrix is complex
	b->xtype = CHOLMOD_REAL; //type of the value arrays (x, z). Options: CHOLMOD_PATTERN, CHOLMOD_REAL, CHOLMOD_COMPLEX, CHPLMOD_ZOMPLEX
	b->dtype = CHOLMOD_DOUBLE; //precision of value arrays. Options: CHOLMOD_REAL, CHOLMOD_FLOAT

	//Solve the system
	cholmod_dense *solution = cholmod_solve(CHOLMOD_A, factorized_matrix, b, common);

	//Copy the solution to the out buffer
	memcpy(out_solution, solution->x, order * sizeof(double));

	//Free the temp dense matrix objects
	b->x = NULL; //The rhs array belongs to the caller and should not be freed by cholmod_free_dense().
	cholmod_free_dense(&b, common);
	cholmod_free_dense(&solution, common);
}
