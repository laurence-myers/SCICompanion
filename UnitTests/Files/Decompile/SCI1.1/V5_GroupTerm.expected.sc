;;; Sierra Script 1.0 - (do not remove this comment)
(script# 976)
(include sci.sh)
(use V5_FormatPrint)

(public
	v5GroupTerm 0
	v5GroupOrStatement 1
	v5GroupTextTuple 2
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

(procedure (v5GroupTextTuple param1 param2 &tmp temp0)
	(if (and param1 ((FormatPrint 976 0) param2))
		(= temp0 1)
		; Group
	else
		(= temp0 2)
	)
)
