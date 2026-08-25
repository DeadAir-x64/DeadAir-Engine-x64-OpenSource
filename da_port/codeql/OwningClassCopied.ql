/**
 * @name Класс освобождает ресурс в деструкторе, копируется неявно и где-то копируется
 * @description Деструктор делает delete/xr_delete/xr_free, своего копирующего конструктора нет,
 *              значит копия поверхностная и её деструктор освободит чужие данные. Третье условие -
 *              что копия реально где-то создаётся - отсекает теоретические случаи.
 * @kind problem
 * @problem.severity error
 * @id da-port/owning-class-copied
 */

import cpp

predicate destructorReleases(Class c) {
  exists(Destructor d |
    d.getDeclaringType() = c and
    not d.isCompilerGenerated() and
    (
      exists(DeleteExpr e | e.getEnclosingFunction() = d)
      or
      exists(DeleteArrayExpr e | e.getEnclosingFunction() = d)
      or
      exists(FunctionCall fc |
        fc.getEnclosingFunction() = d and
        fc.getTarget().getName().regexpMatch("xr_delete|xr_free|_FREE|free|Release|Destroy.*")
      )
    )
  )
}

predicate hasOwnCopyCtor(Class c) {
  exists(CopyConstructor cc | cc.getDeclaringType() = c and not cc.isCompilerGenerated())
}

from Class c, CopyConstructor cc, FunctionCall use
where
  destructorReleases(c) and
  not hasOwnCopyCtor(c) and
  cc.getDeclaringType() = c and
  use.getTarget() = cc and
  not c.getFile().getAbsolutePath().matches("%Externals%") and
  not c.getFile().getAbsolutePath().matches("%3rd party%")
select use, "Копия $@: деструктор освобождает ресурс, а копирующий конструктор неявный.", c, c.getName()
