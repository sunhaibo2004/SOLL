// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "soll/CodeGen/FuncBodyCodeGen.h"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>

using namespace soll;
using llvm::BasicBlock;
using llvm::Function;
using llvm::FunctionType;
using llvm::Value;

void FuncBodyCodeGen::compile(const soll::FunctionDecl &FD) {
  // TODO: replace this temp impl
  // this impl assumes type of functionDecl params and return is uint64
  auto PsSol = FD.getParams()->getParams();
  CurFunc = Module.getFunction(FD.getName());
  if ( CurFunc == nullptr ) {
    std::vector<llvm::Type *> Tys;
    for(int i = 0; i < PsSol.size(); i++)
      Tys.push_back(Builder.getInt64Ty());
    llvm::ArrayRef<llvm::Type *> ParamTys(&Tys[0], Tys.size());
    FunctionType *FT = FunctionType::get(Builder.getInt64Ty(), ParamTys, false);
    CurFunc =
        Function::Create(FT, Function::ExternalLinkage, FD.getName(), &Module);
  }
  BasicBlock *BB = BasicBlock::Create(Context, "entry", CurFunc);
  Builder.SetInsertPoint(BB);

  auto PsLLVM = CurFunc->arg_begin();
  for (int i = 0; i < PsSol.size(); i++) {
    llvm::Value *P = PsLLVM++;
    P->setName(PsSol[i]->getName());
    ParamTable[P->getName()] = P;
  }

  FD.getBody()->accept(*this);
}

void FuncBodyCodeGen::visit(BlockType &B) { ConstStmtVisitor::visit(B); }

void FuncBodyCodeGen::visit(IfStmtType & IF) {
  const bool Else_exist = ( IF.getElse() != nullptr );
  BasicBlock *ThenBB, *ElseBB, *EndBB;
  ThenBB = BasicBlock::Create(Context, "if.then", CurFunc);
  if (Else_exist) {
    ElseBB = BasicBlock::Create(Context, "if.else", CurFunc);
  }
  EndBB = BasicBlock::Create(Context, "if.end", CurFunc);

  IF.getCond()->accept(*this);
  Value* cond = Builder.CreateICmpNE(
    findTempValue(IF.getCond()),
    Builder.getFalse(),
    "cond"
  );
  Builder.CreateCondBr(
    cond,
    ThenBB,
    Else_exist ? ElseBB : EndBB
  );

  Builder.SetInsertPoint(ThenBB);
  IF.getThen()->accept(*this);
  Builder.CreateBr(EndBB);

  if (Else_exist) {
    Builder.SetInsertPoint(ElseBB);
    IF.getElse()->accept(*this);
    Builder.CreateBr(EndBB);
  }

  Builder.SetInsertPoint(EndBB);
}

void FuncBodyCodeGen::visit(WhileStmtType &While) {
  BasicBlock *CondBB = BasicBlock::Create(Context, "while.cond", CurFunc);
  BasicBlock *BodyBB = BasicBlock::Create(Context, "while.body", CurFunc);
  BasicBlock *EndBB = BasicBlock::Create(Context, "while.end", CurFunc);

  Builder.CreateBr(While.isDoWhile() ? BodyBB : CondBB);
  Builder.SetInsertPoint(CondBB);
  While.getCond()->accept(*this);
  Value* cond = Builder.CreateICmpNE(
    findTempValue(While.getCond()),
    Builder.getFalse(),
    "cond"
  );
  Builder.CreateCondBr(
    cond,
    BodyBB,
    EndBB
  );

  Builder.SetInsertPoint(BodyBB);
  While.getBody()->accept(*this);
  Builder.CreateBr(CondBB);

  Builder.SetInsertPoint(EndBB);
}

void FuncBodyCodeGen::visit(ForStmtType &FS) {
  const bool Init_exist = ( FS.getInit() != nullptr );
  const bool Cond_exist = ( FS.getCond() != nullptr );
  const bool Loop_exist = ( FS.getLoop() != nullptr );

  BasicBlock *CondBB = BasicBlock::Create(Context, "for.cond", CurFunc);
  BasicBlock *BodyBB = BasicBlock::Create(Context, "for.body", CurFunc);
  BasicBlock *EndBB = BasicBlock::Create(Context, "for.end", CurFunc);

  if ( Init_exist ) {
    FS.getInit()->accept(*this);
  }
  Builder.CreateBr(CondBB);

  Builder.SetInsertPoint(CondBB);
  if ( Cond_exist ) {
    FS.getCond()->accept(*this);
    Value* cond = Builder.CreateICmpNE(
      findTempValue(FS.getCond()),
      Builder.getFalse()
    );
    Builder.CreateCondBr(
      cond,
      BodyBB,
      EndBB
    );
  }
  else Builder.CreateBr(BodyBB);

  Builder.SetInsertPoint(BodyBB);
  FS.getBody()->accept(*this);
  if ( Loop_exist ) {
    FS.getLoop()->accept(*this);
  }
  Builder.CreateBr(CondBB);

  Builder.SetInsertPoint(EndBB);
}

void FuncBodyCodeGen::visit(ContinueStmtType &) {
  // TODO
}

void FuncBodyCodeGen::visit(BreakStmtType &) {
  // TODO
}

void FuncBodyCodeGen::visit(ReturnStmtType &RS) {
  if (RS.getRetValue() == nullptr) {
    Builder.CreateRetVoid();
  } else {
    RS.getRetValue()->accept(*this);
    Builder.CreateRet(findTempValue(RS.getRetValue()));
  }
}

void FuncBodyCodeGen::visit(DeclStmtType &DS) {
  // TODO: replace this temp impl
  // this impl assumes declared variables are uint64
  for (auto &D : DS.getVarDecls()) {
    auto *p = Builder.CreateAlloca(llvm::Type::getInt64Ty(Context), nullptr,
                                   D->getName() + "_addr");
    LocalVarAddrTable[D->getName()] = p;
  }
  // TODO: replace this
  // this impl. assumes no tuple expression;
  if (DS.getValue() != nullptr) {
    DS.getValue()->accept(*this);
    Builder.CreateStore(findTempValue(DS.getValue()),
                        findLocalVarAddr(DS.getVarDecls()[0]->getName()));
  }
}

void FuncBodyCodeGen::visit(UnaryOperatorType &UO) {
  ConstStmtVisitor::visit(UO);
  llvm::Value *V = nullptr;
  if (UO.isArithmeticOp()) {
    llvm::Value *subVal = findTempValue(UO.getSubExpr());
    if (UO.getSubExpr()->isLValue()) {
      subVal = Builder.CreateLoad(subVal);
    }
    switch (UO.getOpcode()) {
    case UnaryOperatorKind::UO_Plus: 
      V = subVal;
      break;
    case UnaryOperatorKind::UO_Minus:
      V = Builder.CreateNeg(subVal, "UO_Minus");
      break;
    case UnaryOperatorKind::UO_Not: 
      V = Builder.CreateNot(subVal, "UO_Not");
      break;
    case UnaryOperatorKind::UO_LNot: 
      V = Builder.CreateICmpEQ(Builder.getInt64(0), subVal, "UO_LNot");
      break;
    default:
      ;
    }
  } else {
    // assume subExpr is a LValue
    llvm::Value *subRef = findTempValue(UO.getSubExpr());   // the LValue(address) of subexpr
    llvm::Value *subVal = nullptr;                          // to store value of subexpr if needed
    llvm::Value *tmp = nullptr;
    switch (UO.getOpcode()) {
    case UnaryOperatorKind::UO_PostInc: 
      subVal = Builder.CreateLoad(subRef);
      tmp = Builder.CreateAdd(subVal, Builder.getInt64(1), "UO_PostInc");
      Builder.CreateStore(tmp, subRef);
      V = subVal;
      break;
    case UnaryOperatorKind::UO_PostDec:
      subVal = Builder.CreateLoad(subRef);
      tmp = Builder.CreateSub(subVal, Builder.getInt64(1), "UO_PostDec");
      Builder.CreateStore(tmp, subRef);
      V = subVal;
      break;
    case UnaryOperatorKind::UO_PreInc:
      subVal = Builder.CreateLoad(subRef);
      tmp = Builder.CreateAdd(subVal, Builder.getInt64(1), "UO_PreInc");
      Builder.CreateStore(tmp, subRef);
      V = subRef;
      break;
    case UnaryOperatorKind::UO_PreDec:
      subVal = Builder.CreateLoad(subRef);
      tmp = Builder.CreateSub(subVal, Builder.getInt64(1), "UO_PreDec");
      Builder.CreateStore(tmp, subRef);
      V = subRef;
      break;
    default:
      ;
    }
  }
  TempValueTable[&UO] = V;
}

void FuncBodyCodeGen::visit(BinaryOperatorType &BO) {
  // TODO: replace this temp impl (visit(BinaryOperatorType &BO))
  // This impl assumes:
  //   every type is uint64
  ConstStmtVisitor::visit(BO);
  llvm::Value *V = nullptr;
  if (BO.isAssignmentOp()) {
    // TODO: replace this temp impl
    // because we havn't properly dealed with lvalue/rvalue
    // This impl assumes:
    //   lhs of assignment operator (=, +=, -=, ...) is a Identifier,

    llvm::Value *lhsAddr = findTempValue(BO.getLHS()); // required lhs as LValue
    llvm::Value *lhsVal = nullptr;
    llvm::Value *rhsVal = findTempValue(BO.getRHS());
    if (BO.getRHS()->isLValue()) {
      rhsVal = Builder.CreateLoad(rhsVal);
    }
    switch (BO.getOpcode()) {
    case BO_Assign:
      lhsVal = Builder.CreateStore(rhsVal, lhsAddr);
      break;
    case BO_MulAssign:
      lhsVal = Builder.CreateLoad(lhsAddr);
      lhsVal = Builder.CreateMul(lhsVal, rhsVal, "BO_MulAssign");
      Builder.CreateStore(lhsVal, lhsAddr);
      break;
    case BO_DivAssign:
      lhsVal = Builder.CreateLoad(lhsAddr);
      lhsVal = Builder.CreateUDiv(lhsVal, rhsVal, "BO_DivAssign");
      Builder.CreateStore(lhsVal, lhsAddr);
      break;
    case BO_RemAssign:
      lhsVal = Builder.CreateLoad(lhsAddr);
      lhsVal = Builder.CreateURem(lhsVal, rhsVal, "BO_RemAssign");
      Builder.CreateStore(lhsVal, lhsAddr);
      break;
    case BO_AddAssign:
      lhsVal = Builder.CreateLoad(lhsAddr);
      lhsVal = Builder.CreateAdd(lhsVal, rhsVal, "BO_AddAssign");
      Builder.CreateStore(lhsVal, lhsAddr);
      break;
    case BO_SubAssign:
      lhsVal = Builder.CreateLoad(lhsAddr);
      lhsVal = Builder.CreateSub(lhsVal, rhsVal, "BO_SubAssign");
      Builder.CreateStore(lhsVal, lhsAddr);
      break;
    case BO_ShlAssign:
      lhsVal = Builder.CreateLoad(lhsAddr);
      lhsVal = Builder.CreateShl(lhsVal, rhsVal, "BO_ShlAssign");
      Builder.CreateStore(lhsVal, lhsAddr);
      break;
    case BO_ShrAssign:
      lhsVal = Builder.CreateLoad(lhsAddr);
      lhsVal = Builder.CreateAShr(lhsVal, rhsVal, "BO_ShrAssign");
      Builder.CreateStore(lhsVal, lhsAddr);
      break;
    case BO_AndAssign:
      lhsVal = Builder.CreateLoad(lhsAddr);
      lhsVal = Builder.CreateAnd(lhsVal, rhsVal, "BO_AndAssign");
      Builder.CreateStore(lhsVal, lhsAddr);
      break;
    case BO_XorAssign:
      lhsVal = Builder.CreateLoad(lhsAddr);
      lhsVal = Builder.CreateXor(lhsVal, rhsVal, "BO_XorAssign");
      Builder.CreateStore(lhsVal, lhsAddr);
      break;
    case BO_OrAssign:
      lhsVal = Builder.CreateLoad(lhsAddr);
      lhsVal = Builder.CreateOr(lhsVal, rhsVal, "BO_OrAssign");
      Builder.CreateStore(lhsVal, lhsAddr);
      break;
    default:;
    }
    V = rhsVal;
  }

  if (BO.isAdditiveOp() || BO.isMultiplicativeOp() || BO.isComparisonOp() || BO.isShiftOp() || BO.isBitwiseOp()) {
    llvm::Value *lhs = findTempValue(BO.getLHS());
    llvm::Value *rhs = findTempValue(BO.getRHS());
    if (BO.getLHS()->isLValue()) {
      lhs = Builder.CreateLoad(lhs);
    }
    if (BO.getRHS()->isLValue()) {
      rhs = Builder.CreateLoad(rhs);
    }
    switch (BO.getOpcode()) {
    case BinaryOperatorKind::BO_Add:
      V = Builder.CreateAdd(lhs, rhs, "BO_ADD");
      break;
    case BinaryOperatorKind::BO_Sub:
      V = Builder.CreateSub(lhs, rhs, "BO_SUB");
      break;
    case BinaryOperatorKind::BO_Mul:
      V = Builder.CreateMul(lhs, rhs, "BO_MUL");
      break;
    case BinaryOperatorKind::BO_Div:
      V = Builder.CreateUDiv(lhs, rhs, "BO_DIV");
      break;
    case BinaryOperatorKind::BO_Rem:
      V = Builder.CreateURem(lhs, rhs, "BO_Rem");
      break;
    case BinaryOperatorKind::BO_GE:
      V = Builder.CreateICmpUGE(lhs, rhs, "BO_UGE");
      break;
    case BinaryOperatorKind::BO_GT:
      V = Builder.CreateICmpUGT(lhs, rhs, "BO_UGT");
      break;
    case BinaryOperatorKind::BO_LE:
      V = Builder.CreateICmpULE(lhs, rhs, "BO_ULE");
      break;
    case BinaryOperatorKind::BO_LT:
      V = Builder.CreateICmpULT(lhs, rhs, "BO_ULT");
      break;
    case BinaryOperatorKind::BO_EQ:
      V = Builder.CreateICmpEQ(lhs, rhs, "BO_EQ");
      break;
    case BinaryOperatorKind::BO_NE:
      V = Builder.CreateICmpNE(lhs, rhs, "BO_NE");
      break;
    case BinaryOperatorKind::BO_Shl:
      V = Builder.CreateShl(lhs, rhs, "BO_Shl");
      break;
    case BinaryOperatorKind::BO_Shr:
      V = Builder.CreateAShr(lhs, rhs, "BO_Shr");
      break;
    case BinaryOperatorKind::BO_And:
      V = Builder.CreateAnd(lhs, rhs, "BO_And");
      break;
    case BinaryOperatorKind::BO_Xor:
      V = Builder.CreateXor(lhs, rhs, "BO_Xor");
      break;
    case BinaryOperatorKind::BO_Or: 
      V = Builder.CreateOr(lhs, rhs, "BO_Or");
      break;
    default:;
    }
  }

  if (BO.isLogicalOp()) {
    llvm::Value *lhs = findTempValue(BO.getLHS());
    llvm::Value *rhs = findTempValue(BO.getRHS());
    if (BO.getLHS()->isLValue()) {
      lhs = Builder.CreateLoad(lhs);
    }
    if (BO.getRHS()->isRValue()) {
      rhs = Builder.CreateLoad(rhs);
    }
    if (BO.getOpcode() == BO_LAnd) {
      llvm::Value *res = Builder.CreateAlloca(Builder.getInt64Ty());
      BasicBlock *trueBB = BasicBlock::Create(Context, "BO_LAnd.true", CurFunc);
      BasicBlock *falseBB =
          BasicBlock::Create(Context, "BO_LAnd.false", CurFunc);
      BasicBlock *endBB = BasicBlock::Create(Context, "BO_LAnd.end", CurFunc);

      BO.getLHS()->accept(*this);
      llvm::Value *lhs = findTempValue(BO.getLHS());
      if (BO.getLHS()->isLValue()) {
        lhs = Builder.CreateLoad(lhs);
      }
      llvm::Value *isTrueLHS =
          Builder.CreateICmpNE(lhs, Builder.getInt64(0));
      llvm::Value *truncLHS =
          Builder.CreateTrunc(isTrueLHS, Builder.getInt1Ty(), "trunc");
      Builder.CreateCondBr(truncLHS, trueBB, falseBB);

      Builder.SetInsertPoint(trueBB);
      BO.getRHS()->accept(*this);
      llvm::Value *rhs = findTempValue(BO.getRHS());
      if (BO.getRHS()->isLValue()) {
        rhs = Builder.CreateLoad(rhs);
      }
      llvm::Value *isTrueRHS =
          Builder.CreateICmpNE(rhs, Builder.getInt64(0));
      Builder.CreateStore(Builder.CreateZExt(isTrueRHS, Builder.getInt64Ty()),
                          res);
      Builder.CreateBr(endBB);

      Builder.SetInsertPoint(falseBB);
      Builder.CreateStore(Builder.getInt64(0), res);
      Builder.CreateBr(endBB);

      Builder.SetInsertPoint(endBB);
      // in order to store result of LAnd in TempValueTable
      V = Builder.CreateLoad(res, "BO_LAnd");
    } else if (BO.getOpcode() == BO_LOr) {
      llvm::Value *res = Builder.CreateAlloca(Builder.getInt64Ty());
      BasicBlock *trueBB = BasicBlock::Create(Context, "BO_LOr.true", CurFunc);
      BasicBlock *falseBB =
          BasicBlock::Create(Context, "BO_LOr.false", CurFunc);
      BasicBlock *endBB = BasicBlock::Create(Context, "BO_LOr.end", CurFunc);

      BO.getLHS()->accept(*this);
      llvm::Value *lhs = findTempValue(BO.getLHS());
      if (BO.getLHS()->isLValue()) {
        lhs = Builder.CreateLoad(lhs);
      }
      llvm::Value *isTrueLHS =
          Builder.CreateICmpNE(lhs, Builder.getInt64(0));
      llvm::Value *trunc =
          Builder.CreateTrunc(isTrueLHS, Builder.getInt1Ty(), "trunc");
      Builder.CreateCondBr(trunc, trueBB, falseBB);

      Builder.SetInsertPoint(trueBB);
      Builder.CreateStore(Builder.getInt64(1), res);
      Builder.CreateBr(endBB);

      Builder.SetInsertPoint(falseBB);
      BO.getRHS()->accept(*this);
      llvm::Value *rhs = findTempValue(BO.getRHS());
      if (BO.getRHS()->isLValue()) {
        rhs = Builder.CreateLoad(rhs);
      }
      llvm::Value *isTrueRHS =
          Builder.CreateICmpNE(rhs, Builder.getInt64(0));
      Builder.CreateStore(Builder.CreateZExt(isTrueRHS, Builder.getInt64Ty()),
                          res);
      Builder.CreateBr(endBB);

      Builder.SetInsertPoint(endBB);
      // in order to store result of LAnd in TempValueTable
      V = Builder.CreateLoad(res, "BO_LOr");
    }
  }
  TempValueTable[&BO] = V;
}

void FuncBodyCodeGen::visit(CallExprType &CALL) {
  auto Name = dynamic_cast<const Identifier *>(CALL.getCalleeExpr())->getName();
  if (Name.compare("require") == 0) {
    auto Arguments = CALL.getArguments();
    Arguments[0]->accept(*this);
    Value *CondV = findTempValue(Arguments[0]);
    Arguments[1]->accept(*this);
    Value *Length = Builder.getInt32(
        dynamic_cast<const StringLiteral *>(Arguments[1])->getValue().length() +
        1);

    BasicBlock *RevertBB = BasicBlock::Create(Context, "revert", CurFunc);
    BasicBlock *ContBB = BasicBlock::Create(Context, "continue", CurFunc);

    Builder.CreateCondBr(CondV, ContBB, RevertBB);
    Builder.SetInsertPoint(RevertBB);
    auto *MSG = Builder.CreateInBoundsGEP(
        findTempValue(Arguments[1]), {Builder.getInt32(0), Length}, "msg.ptr");
    Builder.CreateCall(Module.getFunction("revert"), {MSG, Length});
    Builder.CreateUnreachable();

    Builder.SetInsertPoint(ContBB);
  }
}

void FuncBodyCodeGen::visit(ParenExprType &P) {
  ConstStmtVisitor::visit(P);
  TempValueTable[&P] = findTempValue(P.getSubExpr());
}

void FuncBodyCodeGen::visit(IdentifierType &ID) {
  // TODO: replace this temp impl
  // this impl assumes visited Identifier is lvalue
  llvm::Value *V = nullptr;
  if (llvm::Value *Addr = findLocalVarAddr(ID.getName())) {
    V = Addr;
  } else if (llvm::Value *Val = findParam(ID.getName())) {
    // V = Address of the found param;
  } else {
    // V = Builder.getInt64(7122); // TODO: replace this
  }

  TempValueTable[&ID] = V;
}

void FuncBodyCodeGen::visit(BooleanLiteralType &BL) {
  TempValueTable[&BL] = Builder.getInt1(BL.getValue());
}

void FuncBodyCodeGen::visit(StringLiteralType &SL) {
  TempValueTable[&SL] = Builder.CreateGlobalString(SL.getValue(), "str");
}

void FuncBodyCodeGen::visit(NumberLiteralType &NL) {
  TempValueTable[&NL] = Builder.getInt64(NL.getValue());
}
