/**
 * @name VERIFY-guarded value dereferenced (stripped in release)
 * @description Значение, проверенное макросом VERIFY (в релизе он `do{}while(false)`), затем
 *              разыменовывается/индексируется/делится. Если значение приходит из ДАННЫХ
 *              (конфиг/сейв/сеть/скрипт), в релизе это краш. Ищем: VERIFY(v ...) и последующее
 *              обращение к тому же v в той же функции, приоритет — если v получен из data-source.
 * @kind problem
 * @problem.severity warning
 * @id da/verify-stripped-deref
 * @tags reliability
 *       correctness
 */

import cpp

predicate isVerifyMacro(Macro m) {
  m.getName() = "VERIFY" or
  m.getName() = "VERIFY2" or
  m.getName() = "VERIFY3" or
  m.getName() = "VERIFY4"
}

/** Функция-источник «внешних данных»: конфиг/сейв/сеть/скрипт/реестр по номеру. */
predicate isDataSourceCall(FunctionCall fc) {
  exists(string n | n = fc.getTarget().getName() |
    n.matches("r\\_u%") or n.matches("r\\_s%") or n = "r_float" or n = "r_stringZ" or
    n.matches("r\\_string%") or n = "smart_cast" or n = "dynamic_cast" or
    n.matches("%find%") or n = "object" or n.matches("%_by_id") or n = "r_string" or
    n = "IndexToId" or n = "LL_GetMotionDef" or n = "get_upgrade" or n = "ReadAttrib"
  )
}

/** Переменная v упоминается в аргументе VERIFY. */
predicate verifyChecks(MacroInvocation mi, Variable v) {
  isVerifyMacro(mi.getMacro()) and
  exists(VariableAccess va | va = mi.getAnExpandedElement*() and va.getTarget() = v)
}

/** Разыменование/индексация/поле/деление переменной v. */
predicate dangerousUse(VariableAccess use, Variable v) {
  use.getTarget() = v and
  (
    exists(PointerDereferenceExpr d | d.getOperand() = use) or
    exists(FieldAccess fa | fa.getQualifier() = use) or
    exists(ArrayExpr ae | ae.getArrayBase() = use or ae.getArrayOffset() = use) or
    exists(DivExpr de | de.getRightOperand() = use) or
    exists(RemExpr re | re.getRightOperand() = use)
  )
}

from MacroInvocation mi, Variable v, VariableAccess use
where
  verifyChecks(mi, v) and
  dangerousUse(use, v) and
  // обращение ПОСЛЕ макроса, в той же функции
  use.getEnclosingFunction() = mi.getEnclosingFunction() and
  use.getLocation().getStartLine() > mi.getLocation().getEndLine() and
  use.getLocation().getStartLine() < mi.getLocation().getEndLine() + 8 and
  // приоритет: значение v связано с data-source в той же функции
  exists(FunctionCall fc | isDataSourceCall(fc) and fc.getEnclosingFunction() = use.getEnclosingFunction())
select use, "VERIFY-проверенное значение '" + v.getName() +
  "' разыменовано после макроса (в релизе VERIFY вырезан); рядом есть источник данных. VERIFY на $@.",
  mi, mi.getMacroName()
