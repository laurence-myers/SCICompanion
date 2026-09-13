;;; Sierra Script 1.0 - (do not remove this comment)
(script# 982)
(include sci.sh)
(use Main)

(public
	IsOnScreen 0
	CantBeSeen 1
	AngleDiff 2
)

(procedure (IsOnScreen param1)
	(return
		(not
			(if
				(and
					(<= 0 (param1 x?))
					(<= (param1 x?) 319)
					(<= 0 (- (param1 y?) (param1 z?)))
				)
				(<= (- (param1 y?) (param1 z?)) 189)
			else
				0
			)
		)
	)
)

(procedure (CantBeSeen param1 theTheGEgo param3 param4 &tmp theGEgo temp1 temp2 temp3 temp4 theGEgoX theGEgoY)
	(= theGEgo theTheGEgo)
	(= temp1 param3)
	(= temp2 param4)
	(if (< argc 4)
		(= temp2 32767)
		(if (< argc 3)
			(if (< argc 2) (= theGEgo gEgo))
			(= temp1
				(-
					360
					(if (== theGEgo gEgo)
						(* 2 (theGEgo sightAngle?))
					else
						0
					)
				)
			)
		)
	)
	(= temp3 (param1 x?))
	(= temp4 (param1 y?))
	(= theGEgoX (theGEgo x?))
	(= theGEgoY (theGEgo y?))
	(cond 
		((== param1 theGEgo) 0)
		(
			(or
				(<
					(/ temp1 2)
					(Abs
						(AngleDiff
							(GetAngle theGEgoX theGEgoY temp3 temp4)
							(theGEgo heading?)
						)
					)
				)
				(<
					temp2
					(GetDistance theGEgoX theGEgoY temp3 temp4 gPicAngle)
				)
			)
			(return 1)
		)
		(else (return 0))
	)
)

(procedure (AngleDiff param1 param2)
	(if (>= argc 2) (-= param1 param2))
	(return
		(cond 
			((<= param1 -180) (+ param1 360))
			((> param1 180) (- param1 360))
			(else param1)
		)
	)
)
