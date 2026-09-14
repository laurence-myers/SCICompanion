;;; Sierra Script 1.0 - (do not remove this comment)
(script# 931)
(include sci.sh)

(public
	f13ValueIfArgument 0
	f13TwoValueIfArguments 1
	f13DroppedLoadArgument 2
)

(procedure (f13ValueIfArgument param1 param2 param3 &tmp temp0)
	(= temp0
		(param3
			posn: 0 (if (> param1 param2) param2 else param1)
		)
	)
)

(procedure (f13TwoValueIfArguments param1 param2 param3 &tmp temp0)
	(= temp0
		(param3
			posn:
				(if (> param1 param2) (- param2 param1) else 0)
				(if (> param1 param2) param2 else param1)
		)
	)
)

(procedure (f13DroppedLoadArgument param1 param2 param3 &tmp temp0 temp1)
	(= temp1 (param3 size?))
	(= temp0
		(param3
			posn:
				(if (> param1 temp1) (- temp1 param1) else 0)
				(if (> param1 temp1) temp1 else param1)
		)
	)
)
