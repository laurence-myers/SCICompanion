;;; Sierra Script 1.0 - (do not remove this comment)
(script# 945)
(include sci.sh)
(use Main)
(use Cycle)
(use System)


(class PolyPath of Motion
	(properties
		client 0
		caller 0
		x 0
		y 0
		dx 0
		dy 0
		b-moveCnt 0
		b-i1 0
		b-i2 0
		b-di 0
		b-xAxis 0
		b-incr 0
		completed 0
		xLast 0
		yLast 0
		value 2
		points 0
		finalX 0
		finalY 0
		obstacles 0
	)
	
	(method (init theClient theFinalX theFinalY theCaller param5 theObstacles &tmp [temp0 30])
		(if argc
			(= client theClient)
			(if (> argc 1)
				(cond 
					((>= argc 6) (= obstacles theObstacles))
					((not (IsObject obstacles)) (= obstacles (gRoom obstacles?)))
				)
				(if points (Memory 3 points))
				(= points
					(AvoidPath
						(theClient x?)
						(theClient y?)
						(= finalX theFinalX)
						(= finalY theFinalY)
						(and obstacles (obstacles elements?))
						(and obstacles (obstacles size?))
						(if (>= argc 5) param5 else 1)
					)
				)
				(if (> argc 3) (= caller theCaller))
			)
			(self setTarget:)
		)
		(super init:)
	)
	
	(method (dispose)
		(if points (Memory 3 points))
		(= points 0)
		(super dispose:)
	)
	
	(method (moveDone)
		(if (== (WordAt points value) 30583)
			(super moveDone:)
		else
			(self setTarget: init:)
		)
	)
	
	(method (setTarget &tmp temp0 theX theY gAltPolyListSize [temp4 30])
		(if (!= (WordAt points value) 30583)
			(= x (WordAt points value))
			(= y (WordAt points (++ value)))
			(++ value)
			(if
				(and
					(IsObject gAltPolyList)
					(= gAltPolyListSize (gAltPolyList size?))
				)
				(= temp0
					(AvoidPath
						(client x?)
						(client y?)
						x
						y
						(gAltPolyList elements?)
						gAltPolyListSize
						0
					)
				)
				(= theX (WordAt temp0 2))
				(= theY (WordAt temp0 3))
				(if (or (!= x theX) (!= y theY))
					(= x theX)
					(= y theY)
					(Memory 6 (+ points value 2) 30583)
				)
				(Memory 3 temp0)
			)
		)
	)
)
