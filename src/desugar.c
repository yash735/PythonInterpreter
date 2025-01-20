/*  -*- Mode: C; -*-                                                        */
/*                                                                          */
/*  desugar.c                                                               */
/*                                                                          */
/*  (C) Jamie A. Jennings, 2024                                             */

#include "desugar.h"
#include "util.h"
#include <assert.h>

ast *fixup_let(ast *ls) {
  if (ast_errorp(ls) || ast_atomp(ls))
    return ast_copy(ls);

  const char *start = ls->start;

  if (ast_letp(ls)) {
    // We have a let expression but no surrounding block context
    ast *id = ast_car(ls);
    ast *rhs = ast_car(ast_cdr(ls));
    ast *rest = ast_cdr(ast_cdr(ls));
    ast *block;
    if (!ast_nullp(rest))
      block = fixup_let(ast_car(rest));
    else
      block = ast_null(AST_BLOCK, start);
    return ast_cons(AST_LET,
		    ast_copy(id),
		    ast_cons(AST_LET,
			     fixup_let(rhs),
			     ast_cons(AST_BLOCK,
				      block,
				      ast_null(AST_BLOCK, start))));
  }

  if (ast_blockp(ls)) {
    // Walk through block, looking for 'let'
    ast *new = ast_null(AST_BLOCK, start);
    while (!ast_nullp(ls)) {
      ast *current = ast_car(ls);
      if (!ast_letp(current)) {
	new = ast_cons(AST_BLOCK, fixup_let(current), new);
	ls = ast_cdr(ls);
	continue;
      } 
      // We found a 'let' form
      ast *id = ast_car(current);
      ast *rhs = ast_car(ast_cdr(current));
      if (!ast_nullp(ast_cdr(ast_cdr(current)))) {
	// Optional block is present -- nothing to do here
	new = ast_cons(AST_BLOCK, fixup_let(current), new);
	ls = ast_cdr(ls);
	continue;
      }
      // We found a 'let' form with no optional block.  This use of
      // 'let' treats the rest of the surrounding block as its scope.
      ast *new_id = ast_copy(id);
      ast *new_rhs = fixup_let(rhs);
//       printf("BEFORE:\n");
//       print_ast(rhs);
//       printf("AFTER:\n");
//       print_ast(new_rhs);
//       printf("=====\n");
      ast *new_let_block = fixup_let(ast_cdr(ls));
      ast *new_let = ast_cons(AST_LET,
			      new_id,
			      ast_cons(AST_LET,
				       new_rhs,
				       ast_cons(AST_BLOCK,
						new_let_block,
						ast_null(AST_BLOCK, start))));
      new = ast_cons(AST_BLOCK, new_let, new);
      return ast_nreverse(new);
    } // While block still has expressions left
    return ast_nreverse(new);
  } // If we have a block


  // Else we have some other list-like AST node
  return ast_map(fixup_let, ls);
}
  




// ast *desugar_ast(ast *t) {

// }
