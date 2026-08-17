/**
 * @name smart_cast/dynamic_cast result dereferenced (VERIFY stripped in release)
 * @description Результат smart_cast/dynamic_cast может быть null (приведение чужого объекта из
 *              данных/сети/скрипта). В релизе VERIFY вырезан — разыменование без живой проверки на
 *              null падает. Barrier: значение участвует в управляющей проверке (guard).
 * @kind path-problem
 * @problem.severity warning
 * @id da/smartcast-null-deref
 * @tags reliability correctness
 */

import cpp
import semmle.code.cpp.dataflow.new.DataFlow

/** Живой guard остаётся в релизе: переменная тестируется в каком-либо if этой же функции. */
predicate liveGuarded(Expr e) {
  exists(IfStmt ifs, VariableAccess check |
    check.getTarget() = e.(VariableAccess).getTarget() and
    ifs.getEnclosingFunction() = e.getEnclosingFunction() and
    ifs.getCondition().getAChild*() = check
  )
}

module CastCfg implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node source) {
    exists(FunctionCall fc | fc = source.asExpr() |
      fc.getTarget().getName().matches("smart_cast%") or
      fc.getTarget().getName() = "dynamic_cast"
    )
  }

  predicate isSink(DataFlow::Node sink) {
    exists(FieldAccess fa | fa.getQualifier() = sink.asExpr()) or
    exists(PointerDereferenceExpr d | d.getOperand() = sink.asExpr())
  }

  predicate isBarrier(DataFlow::Node node) { liveGuarded(node.asExpr()) }
}

module CastFlow = DataFlow::Global<CastCfg>;

import CastFlow::PathGraph

from CastFlow::PathNode source, CastFlow::PathNode sink
where CastFlow::flowPath(source, sink)
select sink.getNode(), source, sink,
  "Результат приведения может быть null и разыменован без живой проверки (в релизе VERIFY снят)."
