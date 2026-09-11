;;; Sierra Script 1.0 - (do not remove this comment)
(script# 901)
(include sci.sh)

; Smoke test: a plain procedure that decompiles to high-level code.
(public
	d0Plain 0
)

(procedure (d0Plain param0 &tmp temp0)
	(= temp0 0)
	(if (> param0 5)
		(= temp0 1)
	else
		(= temp0 2)
	)
	(return temp0)
)
