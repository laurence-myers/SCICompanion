;;; Sierra Script 1.0 - (do not remove this comment)
(script# 975)
(include sci.sh)

(public
	v5GroupTerm 0
	v5GroupOrStatement 1
)

(local
	local0
)
(procedure (v5GroupTerm param1 &tmp temp0)
	(cond 
		(
			(and
				(not local0)
				(> param1 5)
				((= local0 1) (Abs param1))
			)
		)
		((not local0) (= temp0 2))
	)
)

(procedure (v5GroupOrStatement param1 param2 param3 &tmp temp0)
	(if (and param1 ((or param2 param3) param3))
		(= temp0 1)
	else
		(= temp0 2)
	)
)
