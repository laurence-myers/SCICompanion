;;; Sierra Script 1.0 - (do not remove this comment)
(script# 941)
(include sci.sh)

; Each procedure ends with an empty last clause. The test sets the target of
; the bnt of that clause to a bad one, as Sierra's compiler did (PQ3 script
; 202, addPoint::changeState; ICEMAN script 968, SmoothLooper::doit).
(public
	emptyLastCase 0
	emptyLastClause 1
	emptyClauseInCase 2
)

(local
	local0
)

; "eq?; bnt; toss": the last case of a switch.
(procedure (emptyLastCase param1)
	(switch param1
		(0 (= local0 1))
		(1)
	)
)

; "bnt; ret": the last clause of a cond at the end of the procedure.
(procedure (emptyLastClause param1)
	(cond
		((== param1 1) (= local0 1))
		((== param1 2))
	)
)

; "bnt; jmp": the last clause of a cond in a case of a switch.
(procedure (emptyClauseInCase param1)
	(switch param1
		(1
			(cond
				((== local0 1) (= local0 2))
				((== local0 3))
			)
		)
		(2 (= local0 4))
	)
)
