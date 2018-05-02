// suitesparse_utilities.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include <string.h>
#include <algorithm>
#include "cholmod.h"
#include "suitesparse_utilities.h"

/*
* Allocates in heap and returns a handle with matrix settings that must be passed to all CHOLMOD functions. Returns NULL if
* any failure occurs.
* param "factorization": 0 for for simplicial factorization, 1 for automatic decidion between supernodal/simplicial factorization,
*		2 for automatic decidion between supernodal/simplicial factorization and after factorization convert to simplicial.
*		Supernodal is usually faster, but to modify the factorized matrix it must be converted to simplicial, though this can be 
*		done automatically.
* param "ordering": 0 for no reordering, 1 for automatic reordering (let suitesparse try some alternatives and keep the best).
*/
__declspec(dllexport) cholmod_common* util_create_common(int factorization, int ordering)
{
	cholmod_common *common = new cholmod_common;
	cholmod_start(common);

	// Simplicial vs supernodal
	if (factorization == 0) common->supernodal = CHOLMOD_SIMPLICIAL;
	else if (factorization == 1)
	{
		common->supernodal = CHOLMOD_AUTO;
		common->final_super = TRUE;
	}
	else if (factorization == 2)
	{
		common->supernodal = CHOLMOD_AUTO;
		common->final_super = FALSE;
	}
	else return NULL;

	// Ordering
	if (ordering == 0)
	{
		common->nmethods = 1;
		common->method[0].ordering = CHOLMOD_NATURAL;
		common->postorder = FALSE; // Not sure about this one.
		//If postordering doesn't change the row order and improves performance of factorization, update or solution, leave it on
	}
	else if (ordering == 1)
	{
		// Do nothing. The default is to try a few and select the best one (param "ordering = 1")
	}
	else return NULL;

	return common;
}

/*
* Frees the memory for the matrix settings.
* param "common": The matrix settings. It will be freed.
*/
void util_destroy_common(cholmod_common** common)
{
	cholmod_finish(*common);
	*common = NULL;
}

/*
* Frees the memory for the factorized matrix.
* param "factorized_matrix": The factorized matrix data. It will be freed.
* param "common": The matrix settings.
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
* param "order": Number of rows = number of columns.
* param "nnz": Number of non zero entries in the upper triangle.
* param "values": Array containing the non zero entries of the upper triangle in column major order. Length = nnz.
* param "row_indices": Array containing the row indices of the non zero entries of the upper triangle. Length = nnz.
* param "col_offsets": Array containing the indices into values (and row_indices) of the first entry of each column.
*		Length = order + 1. The last entry is col_offsets[order] = nnz.
* param "out_factorized_matrix": Out parameter - the factorized upper triangle of the symmetric CSC matrix.
* param "common": The matrix settings.
*/
__declspec(dllexport) int util_factorize_cscupper(int order, int nnz, double* values, int* row_indices,
	int* col_offsets, cholmod_factor** out_factorized_matrix, cholmod_common* common)
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
* Returns the number of non zero entries in the factorized matrix. If anything goes wrong -1 is returned.
* param "factorization": The factorized matrix
*/
__declspec(dllexport) int util_get_factor_nonzeros(cholmod_factor* factorization)
{
	if (factorization == NULL) return -1;
	else return factorization->nzmax;
}

/*
* Caclulates a fill reducing ordering using the Approximate Minimum Degree algorithm for a symmetric sparse matrix. Returns 
* 1 if the reordering is successful, 0 if it failed (e.g. due to exceeding the available memory).
* param "order": Number of rows = number of columns.
* param "nnz": Number of non zero entries in the upper triangle.
* param "row_indices": Array containing the row indices of the non zero entries of the upper triangle. Length = nnz.
* param "col_offsets": Array containing the indices into values (and row_indices) of the first entry of each column.
*		Length = order + 1. The last entry is col_offsets[order] = nnz.
* param "out_permutation": Out parameter - buffer for the computed permutation vector. Length == order. This permutation
*		vector can be intepreted as: original index = i, reordered index = out_permutation[i]
*/
__declspec(dllexport) int util_reorder_amd_upper(int order, int nnz, int* row_indices, int* col_offsets,
	int* out_permutation, cholmod_common* common)
{
	//TODO: Try the AMD package: amd_order and amd_order2. 
	//TODO: Try to store the lower triangle too. Matlab says that symmetric AMD is slower.
	//TODO: Try to not pass the diagonal entries. This would be a significant gain in memory for XFEM reanalysis

	// Create temp sparse matrix object
	cholmod_sparse *A = (cholmod_sparse*)cholmod_malloc(1, sizeof(cholmod_sparse), common);
	A->nrow = order;
	A->ncol = order;
	A->nzmax = nnz;
	A->p = col_offsets;
	A->nz = NULL; // Column indices if the matrix is unpacked.
	A->i = row_indices;
	A->x = NULL;
	A->z = NULL; //imaginary parts if the matrix is complex
	A->stype = 1; // >0: upper triangle is stored, <0: lower triangle is stored, 0: unsymmetric matrix
	A->itype = CHOLMOD_INT; //type of the void index arrays (p,i,nz). Options: CHOLMOD_INT, CHOLMOD_INTLONG, CHOLMOD_LONG
	A->xtype = CHOLMOD_PATTERN; //type of the value arrays (x, z). Options: CHOLMOD_PATTERN, CHOLMOD_REAL, CHOLMOD_COMPLEX, CHPLMOD_ZOMPLEX
	A->dtype = CHOLMOD_DOUBLE; //precision of value arrays. Options: CHOLMOD_REAL, CHOLMOD_FLOAT
	A->sorted = 1; // TRUE (1) if the columns are sorted, FALSE(0) otherwise
	A->packed = 1; // FALSE (0) if the column indexes are stored explicitly in nz, TRUE(1) otherwise (nz = NULL)

	// Reorder
	int* fset = NULL; // These are useful for unsymmetric matrices only
	int fsize = 0; // These are for unsymmetric matrices
	int status = cholmod_amd(A, fset, fsize, out_permutation, common);
	
	// Free temp sparse matrix object. 
	A->p = NULL; //The CSC arrays belong to the caller and should not be freed by cholmod_free_sparse().
	A->i = NULL;
	cholmod_free_sparse(&A, common);

	return status;
}

/*
* Adds a row and column to an LDL' factorization. Before updating the kth row and column of L must be equal to the kth
* row and column of the identity matrix. The row/column to add must be a sparse CSC matrix with dimensions n-by-1,
* where n is the order of the matrix. Returns 1 if the method succeeds, 0 otherwise.
* param "order": Number of rows = number of columns.
* param "L": The data of the cholesky factorization of the matrix. It will be modified.
* param "k": Index of row/column to add.
* param "vector_nnz": Number of non zero entries of the row/column to add.
* param "vector_values": The CSC format values of the row/column to add.
* param "vector_row_indices": The CSC format row indices of the row/column to add.
* param "vector_col_offsets": The CSC format column offsets of the row/column to add.
* param "common": The matrix settings.
*/
_declspec(dllexport) int util_row_add(int order, cholmod_factor* L, int k, int vector_nnz, double* vector_values,
	int* vector_row_indices, int* vector_col_offsets, cholmod_common* common)
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
* Deletes a row and column from an cholesky factorization. After updating the kth row and column of L will be equal to the
* kth row and column of the identity matrix. Returns 1 if the method succeeds, 0 otherwise.
* param "L": The LDL' factorization of the matrix. It will be modified.
* param "k": Index of row/column to delete.
* param "common": The matrix settings.
*/
_declspec(dllexport) int util_row_delete(cholmod_factor* L, int k, cholmod_common* common)
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
* param "order: Number of matrix rows = number of matrix columns = length of right hand side vector.
* param "factorized_matrix": The data of the cholesky factorization of the matrix.
* param "rhs": The right hand side vector. Its length must be equal to the order of the matrix: factorized_matrix->n.
* param "out_solution": Buffer for the left hand side vector (unknown). Its length must be equal to the order of the matrix:
factorized_matrix->n
* param "common": The matrix settings.
*/
__declspec(dllexport) void util_solve(int order, cholmod_factor* factorized_matrix, double* rhs, double* out_solution,
	cholmod_common* common)
{
	// Create temp dense matrix object
	cholmod_dense *b = (cholmod_dense*)cholmod_malloc(1, sizeof(cholmod_dense), common);
	b->nrow = order;
	b->ncol = 1;
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
