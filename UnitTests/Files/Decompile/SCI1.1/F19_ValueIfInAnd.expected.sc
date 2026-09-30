;;; Sierra Script 1.0 - (do not remove this comment)
(script# 943)
(include sci.sh)

(public
	f19ValueIfInAnd 0
	f19ValueIfInOr 1
	f19StoredValueIf 2
	f19StoredValueIfAnd 3
	f19PushedValueIf 4
)

(procedure (f19ValueIfInAnd param1 param2 param3 &tmp temp0)
	(if
		(and
			param1
			(!= param2 2)
			(!= (param3 view?) (if (Random 0) 23 else 16))
			(not (Random 1))
		)
		(= temp0 5)
	else
		(= temp0 6)
	)
)

(procedure (f19ValueIfInOr param1 param2 param3 &tmp temp0)
	(if
		(and
			param3
			(not temp0)
			(or
				(== (param1 loop?) (if (== param2 48) 1 else 0))
				(== (param1 loop?) 3)
			)
		)
		(= temp0 5)
	else
		(= temp0 6)
	)
)

(procedure (f19StoredValueIf param1 param2 &tmp temp0)
	(= temp0 (if (> param1 1) 0 else 1))
	(param2 cel: (if temp0 (param2 lastCel:) else 0) init:)
)

(procedure (f19StoredValueIfAnd param1 param2 param3 &tmp temp0)
	(= temp0 (if (> param1 1) 0 else 1))
	(param2
		cel: (if (and temp0 param3) (param2 lastCel:) else 0)
		init:
	)
)

(procedure (f19PushedValueIf param1 param2 param3 &tmp temp0)
	(if
		(and
			param1
			(== (+ (if param2 13 else 0) 61) param3)
			temp0
		)
		(= temp0 5)
	else
		(= temp0 6)
	)
)
