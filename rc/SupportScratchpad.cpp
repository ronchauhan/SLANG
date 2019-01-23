//===----------------------------------------------------------------------===//
//  MIT License.
//  Copyright (c) 2019 The SLANG Authors.
//
//  Author: Ronak Chauhan (r.chauhan@somaiya.edu)
//  Author: Anshuman Dhuliya (dhuliya@cse.iitb.ac.in)
//
// AD If MyCFGDumper class name is added or changed, then also edit,
// AD ../../../include/clang/StaticAnalyzer/Checkers/Checkers.td
//
//===----------------------------------------------------------------------===//
//
// Expanding the MyDebugCheckers.cpp from 323a0b8 to support more constructs

#include "ClangSACheckers.h"
#include "clang/AST/Decl.h" //AD
#include "clang/AST/Expr.h" //AD
#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/CallGraph.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h" //AD
#include <stack>                      // RC
#include <string>                     //AD

using namespace clang;
using namespace ento;

//===----------------------------------------------------------------------===//
// MyCFGDumper
//===----------------------------------------------------------------------===//

namespace {

class MyCFGDumper : public Checker<check::ASTCodeBody> {
public:
  void checkASTCodeBody(const Decl *D, AnalysisManager &mgr,
                        BugReporter &BR) const;

private:
  enum OperandInfoTy {
    IS_LHS_OP___BOTH_TEMPS = 1,
    IS_RHS_OP___BOTH_TEMPS,
    IS_BEING_ASSIGNED,
    IS_CONDITION_FOR_JUMP,
    IS_MAKING_NEW_TEMP_VAR
  };

  void handleFunction(const FunctionDecl *D) const;

  void handleCfg(const CFG *cfg) const;

  void handleBBInfo(const CFGBlock *bb, const CFG *cfg) const;

  void handleBBStmts(const CFGBlock *bb) const;

  void handleDeclStmt(const DeclStmt *DS,
                      std::stack<const Stmt *> &helper_stack, int &temp_counter,
                      unsigned &block_id) const;

  void handleIntegerLiteral(const Stmt *IL_Stmt) const;

  void handleDeclRefExpr(const Stmt *DRE_Stmt) const;

  void handleUnaryOperator(const Stmt *operand_stmt,
                           std::stack<const UnaryOperator *> &un_op_stack,
                           int &temp_counter, unsigned &block_id,
                           OperandInfoTy operand_info) const;

  void handleBinaryOperator(std::stack<const Stmt *> &helper_stack,
                            int &temp_counter, unsigned &block_id) const;

  void handleOperand(const Stmt *expression_stmt, int &temp_counter,
                     unsigned &block_id, OperandInfoTy operand_info) const;

  void handleTerminator(const Stmt *terminator,
                        std::stack<const Stmt *> &helper_stack,
                        int &temp_counter, unsigned &block_id) const;

}; // class MyCFGDumper

// Main Entry Point. Invokes top level Function and Cfg handlers.
// Invoked once for each source translation unit function.
void MyCFGDumper::checkASTCodeBody(const Decl *D, AnalysisManager &mgr,
                                   BugReporter &BR) const {
  llvm::errs() << "\nBOUND START: SLANG_Generated_Output.\n";

  // PrintingPolicy Policy(mgr.getLangOpts());
  // Policy.TerseOutput = false;
  // Policy.PolishForDeclaration = true;
  // D->print(llvm::errs(), Policy);

  const FunctionDecl *func_decl = dyn_cast<FunctionDecl>(D);
  handleFunction(func_decl);

  if (const CFG *cfg = mgr.getCFG(D)) {
    handleCfg(cfg);
  } else {
    llvm::errs() << "SLANG: ERROR: No CFG for function.\n";
  }

  llvm::errs() << "\nBOUND END  : SLANG_Generated_Output.\n";
} // checkASTCodeBody()

void MyCFGDumper::handleCfg(const CFG *cfg) const {
  for (const CFGBlock *bb : *cfg) {
    handleBBInfo(bb, cfg);
    handleBBStmts(bb);
  }
} // handleCfg

// Gets the function name, paramaters and return type.
void MyCFGDumper::handleFunction(const FunctionDecl *func_decl) const {
  // STEP 1.1: Print function name
  llvm::errs() << "FuncName: ";
  llvm::errs() << func_decl->getNameInfo().getAsString() << "\n";

  // STEP 1.2: Print function parameters.
  std::string Proto;
  llvm::errs() << "Params  : ";
  if (func_decl
          ->doesThisDeclarationHaveABody()) { //& !func_decl->hasPrototype())
    //{
    for (unsigned i = 0, e = func_decl->getNumParams(); i != e; ++i) {
      if (i)
        Proto += ", ";
      const ParmVarDecl *paramVarDecl = func_decl->getParamDecl(i);
      const VarDecl *varDecl = dyn_cast<VarDecl>(paramVarDecl);

      // Parameter type
      QualType T = varDecl->getTypeSourceInfo()
                       ? varDecl->getTypeSourceInfo()->getType()
                       : varDecl->getASTContext().getUnqualifiedObjCPointerType(
                             varDecl->getType());
      Proto += T.getAsString();

      // Parameter name
      Proto += " ";
      Proto += varDecl->getNameAsString();
    }
  }
  llvm::errs() << Proto << "\n";

  // STEP 1.3: Print function return type.
  const QualType returnQType = func_decl->getReturnType();
  llvm::errs() << "ReturnT : " << returnQType.getAsString() << "\n";
} // handleFunction()

void MyCFGDumper::handleBBInfo(const CFGBlock *bb, const CFG *cfg) const {
  unsigned bb_id = bb->getBlockID();

  llvm::errs() << "BB" << bb_id << " ";
  if (bb == &cfg->getEntry())
    llvm::errs() << "[ ENTRY BLOCK ]\n";
  else if (bb == &cfg->getExit())
    llvm::errs() << "[ EXIT BLOCK ]\n";
  else
    llvm::errs() << "\n";

  // details for predecessor blocks
  llvm::errs() << "Predecessors : ";
  if (!bb->pred_empty()) {
    llvm::errs() << bb->pred_size() << "\n              ";

    for (CFGBlock::const_pred_iterator I = bb->pred_begin();
         I != bb->pred_end(); ++I) {
      CFGBlock *B = *I;
      bool Reachable = true;
      if (!B) {
        Reachable = false;
        B = I->getPossiblyUnreachableBlock();
      }
      llvm::errs() << " B" << B->getBlockID();
      if (!Reachable)
        llvm::errs() << " (Unreachable)";
    }
    llvm::errs() << "\n";
  } else {
    llvm::errs() << "None\n";
  }

  // details for successor blocks
  llvm::errs() << "Successors : ";
  if (!bb->succ_empty()) {
    llvm::errs() << bb->succ_size() << "\n            ";

    for (CFGBlock::const_succ_iterator I = bb->succ_begin();
         I != bb->succ_end(); ++I) {
      CFGBlock *B = *I;
      bool Reachable = true;
      if (!B) {
        Reachable = false;
        B = I->getPossiblyUnreachableBlock();
      }

      llvm::errs() << " B" << B->getBlockID();
      if (!Reachable)
        llvm::errs() << "(Unreachable)";
    }
    llvm::errs() << "\n";
  } else {
    llvm::errs() << "None\n";
  }
} // handleBBInfo()

void MyCFGDumper::handleDeclStmt(const DeclStmt *DS,
                                 std::stack<const Stmt *> &helper_stack,
                                 int &temp_counter, unsigned &block_id) const {

  const Decl *decl = DS->getSingleDecl();
  const NamedDecl *named_decl = cast<NamedDecl>(decl);
  QualType T = (cast<ValueDecl>(decl))->getType();

  llvm::errs() << T.getAsString() << " " << named_decl->getNameAsString();

  if (helper_stack.empty()) {
    llvm::errs() << "\n";
    return;
  }

  std::stack<const UnaryOperator *> un_op_stack;
  while (isa<UnaryOperator>(helper_stack.top())) {
    un_op_stack.push(cast<UnaryOperator>(helper_stack.top()));
    helper_stack.pop();
  }
  const Stmt *S = helper_stack.top();
  helper_stack.pop();

  llvm::errs() << " = ";
  handleUnaryOperator(S, un_op_stack, temp_counter, block_id,
                      IS_BEING_ASSIGNED);

  llvm::errs() << "\n";
} // handleDeclStmt()

// Handle subexpressions
void MyCFGDumper::handleOperand(const Stmt *expression_stmt, int &temp_counter,
                                unsigned &block_id,
                                OperandInfoTy operand_info) const {
  switch (expression_stmt->getStmtClass()) {
  case Stmt::IntegerLiteralClass:
    handleIntegerLiteral(expression_stmt);
    break;

  case Stmt::DeclRefExprClass:
    handleDeclRefExpr(expression_stmt);
    break;

  case Stmt::BinaryOperatorClass:
    llvm::errs() << "B" << block_id << ".";
    switch (operand_info) {
    case IS_RHS_OP___BOTH_TEMPS:
      llvm::errs() << temp_counter - 1;
      break;

    case IS_LHS_OP___BOTH_TEMPS:
      llvm::errs() << temp_counter - 2;
      break;

    case IS_BEING_ASSIGNED:
      llvm::errs() << temp_counter;
      break;

    case IS_CONDITION_FOR_JUMP:
      llvm::errs() << temp_counter;
      break;

    case IS_MAKING_NEW_TEMP_VAR:
      llvm::errs() << temp_counter - 1;
      break;
    } // switch
    break;

  default:
    llvm::errs() << "Unhandled " << expression_stmt->getStmtClassName();
    break;
  } // switch
} // handleOperand()

// The idea is as follows
// Before calling this function, we keep pushing UnaryOperator(s) on a stack.
// Then when we actually find our operand, we call this function with the
// operand and the stack. Note that now the top-most element on the stack is the
// most recent UnaryOperator, and so on. Finally we evaluate everything in the
// correct order based on the top element
void MyCFGDumper::handleUnaryOperator(
    const Stmt *operand_stmt, std::stack<const UnaryOperator *> &un_op_stack,
    int &temp_counter, unsigned &block_id, OperandInfoTy operand_info) const {

  if (un_op_stack.empty()) {
    handleOperand(operand_stmt, temp_counter, block_id, operand_info);
  }

  else {
    while (!un_op_stack.empty()) {
      const UnaryOperator *un_op = un_op_stack.top();
      un_op_stack.pop();
      switch (un_op->getOpcode()) {
      case UO_PostInc:
        llvm::errs() << "( ";
        handleUnaryOperator(operand_stmt, un_op_stack, temp_counter, block_id,
                            operand_info);
        llvm::errs() << " )++";
        break;

      case UO_PreInc:
        llvm::errs() << "++( ";
        handleUnaryOperator(operand_stmt, un_op_stack, temp_counter, block_id,
                            operand_info);
        llvm::errs() << " )";
        break;

      case UO_PostDec:
        llvm::errs() << "( ";
        handleUnaryOperator(operand_stmt, un_op_stack, temp_counter, block_id,
                            operand_info);
        llvm::errs() << " )--";
        break;

      case UO_PreDec:
        llvm::errs() << "--( ";
        handleUnaryOperator(operand_stmt, un_op_stack, temp_counter, block_id,
                            operand_info);
        llvm::errs() << " )";
        break;

      case UO_AddrOf:
        llvm::errs() << "&( ";
        handleUnaryOperator(operand_stmt, un_op_stack, temp_counter, block_id,
                            operand_info);
        llvm::errs() << " )";
        break;

      case UO_Deref:
        llvm::errs() << "*( ";
        handleUnaryOperator(operand_stmt, un_op_stack, temp_counter, block_id,
                            operand_info);
        llvm::errs() << " )";
        break;

      case UO_Plus:
      case UO_Minus:
      case UO_Not:
      case UO_LNot:
      case UO_Coawait:
      default:
        llvm::errs() << "UNOP ";
        break;

      } // switch
    }   // while
  }     // else
} // handleUnaryOperator()

void MyCFGDumper::handleBinaryOperator(std::stack<const Stmt *> &helper_stack,
                                       int &temp_counter,
                                       unsigned &block_id) const {

  std::stack<const UnaryOperator *> RHS_un_op_stack;
  std::stack<const UnaryOperator *> LHS_un_op_stack;

  const Stmt *bin_op_stmt = helper_stack.top();
  const BinaryOperator *bin_op = cast<BinaryOperator>(bin_op_stmt);
  helper_stack.pop();

  while (isa<UnaryOperator>(helper_stack.top())) {
    RHS_un_op_stack.push(cast<UnaryOperator>(helper_stack.top()));
    helper_stack.pop();
  }
  Stmt *RHS = const_cast<Stmt *>(helper_stack.top());
  helper_stack.pop();

  while (isa<UnaryOperator>(helper_stack.top())) {
    LHS_un_op_stack.push(cast<UnaryOperator>(helper_stack.top()));
    helper_stack.pop();
  }
  Stmt *LHS = const_cast<Stmt *>(helper_stack.top());
  helper_stack.pop();

  // don't assign temporary variable to assignments
  if (bin_op->isAssignmentOp()) {
    // in case of assignments, RHS is accessed before LHS, hence
    // we swap order
    // auto tmp = LHS;
    // LHS = RHS;
    // RHS = tmp;
    // since operands are swapped, the stacks must also be swapped
    handleUnaryOperator(RHS, RHS_un_op_stack, temp_counter, block_id,
                        IS_BEING_ASSIGNED);
    llvm::errs() << " " << bin_op->getOpcodeStr() << " ";
    handleUnaryOperator(LHS, LHS_un_op_stack, temp_counter, block_id,
                        IS_BEING_ASSIGNED);
  }

  // Assign temporaries otherwise
  else {
    temp_counter++;
    llvm::errs() << "B" << block_id << "." << temp_counter << " = ";

    if (isa<BinaryOperator>(LHS) && isa<BinaryOperator>(RHS)) {
      handleUnaryOperator(LHS, LHS_un_op_stack, temp_counter, block_id,
                          IS_LHS_OP___BOTH_TEMPS);
      llvm::errs() << " " << bin_op->getOpcodeStr() << " ";
      handleUnaryOperator(RHS, RHS_un_op_stack, temp_counter, block_id,
                          IS_RHS_OP___BOTH_TEMPS);
    }

    else {
      handleUnaryOperator(LHS, LHS_un_op_stack, temp_counter, block_id,
                          IS_MAKING_NEW_TEMP_VAR);
      llvm::errs() << " " << bin_op->getOpcodeStr() << " ";
      handleUnaryOperator(RHS, RHS_un_op_stack, temp_counter, block_id,
                          IS_MAKING_NEW_TEMP_VAR);
    }
    helper_stack.push(bin_op_stmt);
  } // else

  llvm::errs() << "\n";
} // handleBinaryOperator()

void MyCFGDumper::handleBBStmts(const CFGBlock *bb) const {

  unsigned bb_id = bb->getBlockID();

  std::stack<const Stmt *> helper_stack;
  int temp_counter = 0; // for naming temporaries

  for (auto elem : *bb) {
    // ref: https://clang.llvm.org/doxygen/CFG_8h_source.html#l00056
    // ref for printing block:
    // https://clang.llvm.org/doxygen/CFG_8cpp_source.html#l05234

    Optional<CFGStmt> CS = elem.getAs<CFGStmt>();
    const Stmt *S = CS->getStmt();

    switch (S->getStmtClass()) {
      // default:
      //   llvm::errs() << S->getStmtClassName() << ".\n";
      //   S->dump();
      //   llvm::errs() << "\n";
      //  break;

    case Stmt::DeclStmtClass:
      handleDeclStmt(cast<DeclStmt>(S), helper_stack, temp_counter, bb_id);
      break;

    case Stmt::BinaryOperatorClass:
      helper_stack.push(S);
      handleBinaryOperator(helper_stack, temp_counter, bb_id);
      break;

    case Stmt::ParenExprClass:
      // simply ignore
      break;

    default:
      if (!isa<ImplicitCastExpr>(S)) {
        // llvm::errs() << S->getStmtClassName() << ".\n";
        helper_stack.push(S);
        // S->dump();
      }
      // llvm::errs() << "\n";
      break;
    } // switch()
  }   // for

  // get terminator
  const Stmt *terminator = (bb->getTerminator()).getStmt();
  handleTerminator(terminator, helper_stack, temp_counter, bb_id);
  llvm::errs() << "\n\n";
} // handleBBStmts()

void MyCFGDumper::handleIntegerLiteral(const Stmt *IL_Stmt) const {
  const IntegerLiteral *IL = cast<IntegerLiteral>(IL_Stmt);
  bool is_signed = IL->getType()->isSignedIntegerType();
  llvm::errs() << IL->getValue().toString(10, is_signed);
}

void MyCFGDumper::handleDeclRefExpr(const Stmt *DRE_Stmt) const {
  const DeclRefExpr *DRE = cast<DeclRefExpr>(DRE_Stmt);
  const ValueDecl *ident = DRE->getDecl();
  llvm::errs() << ident->getName();
}

// TODO: handle ForStmt and GotoStmt and UnaryOperator in conditions
void MyCFGDumper::handleTerminator(const Stmt *terminator,
                                   std::stack<const Stmt *> &helper_stack,
                                   int &temp_counter,
                                   unsigned &block_id) const {
  if (!terminator)
    return;

  Stmt::StmtClass terminator_class = terminator->getStmtClass();
  Expr *condition_expr = nullptr;
  switch (terminator_class) {
  case Stmt::IfStmtClass:
    condition_expr = const_cast<Expr *>(cast<IfStmt>(terminator)->getCond());
    llvm::errs() << "if ";
    break;

  case Stmt::WhileStmtClass:
    condition_expr = const_cast<Expr *>(cast<WhileStmt>(terminator)->getCond());
    llvm::errs() << "while ";
    break;

  default:
    llvm::errs() << "Unhandled terminator - " << terminator->getStmtClassName()
                 << "\n\n";
    terminator->dump();
    break;
  } // switch

  // TODO: some how handle unary operator here
  if (condition_expr) {
    if (isa<BinaryOperator>(condition_expr)) {
      const BinaryOperator *bin_op = cast<BinaryOperator>(condition_expr);
      if (bin_op->isAssignmentOp()) // take LHS of assignment
        handleDeclRefExpr(bin_op->getLHS());
      else // take temporary
        handleOperand(helper_stack.top(), temp_counter, block_id,
                      IS_CONDITION_FOR_JUMP);
    } else {
      // take whatever is on top of the stack
      handleOperand(helper_stack.top(), temp_counter, block_id,
                    IS_CONDITION_FOR_JUMP);
    }
  }
  llvm::errs() << "\n";
} // handleTerminator()

} // anonymous namespace

// Register the Checker
void ento::registerMyCFGDumper(CheckerManager &mgr) {
  mgr.registerChecker<MyCFGDumper>();
}