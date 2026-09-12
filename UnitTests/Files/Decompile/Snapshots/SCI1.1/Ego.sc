;;; Sierra Script 1.0 - (do not remove this comment)
(script# 988)
(include sci.sh)
(use Main)
(use PolyPath)
(use Cycle)
(use Actor)


(class Ego of Actor
	(properties
		x 0
		y 0
		z 0
		heading 0
		noun 0
		_case 0
		modNum -1
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		sightAngle 26505
		actions 0
		onMeCheck 26505
		state 0
		approachX 0
		approachY 0
		approachDist 0
		_approachVerbs 0
		yStep 2
		view -1
		loop 0
		cel 0
		priority 0
		underBits 0
		signal 8192
		lsTop 0
		lsLeft 0
		lsBottom 0
		lsRight 0
		brTop 0
		brLeft 0
		brBottom 0
		brRight 0
		scaleSignal 0
		scaleX 128
		scaleY 128
		maxScale 128
		cycleSpeed 6
		script 0
		cycler 0
		timer 0
		detailLevel 0
		scaler 0
		illegalBits -32768
		xLast 0
		yLast 0
		xStep 3
		origStep 770
		moveSpeed 6
		blocks 0
		baseSetter 0
		mover 0
		looper 0
		viewer 0
		avoider 0
		code 0
		edgeHit 0
	)
	
	(method (init)
		(super init:)
		(if (not cycler) (self setCycle: Walk))
	)
	
	(method (doit)
		(super doit:)
		(= edgeHit
			(cond 
				((<= x gEdgeDistance) 4)
				((>= x (- 319 gEdgeDistance)) 2)
				((>= y (- 189 (/ gEdgeDistance 2))) 3)
				((<= y (gRoom horizon?)) 1)
				(else 0)
			)
		)
	)
	
	(method (handleEvent param1 &tmp temp0 temp1 temp2)
		(= temp1 (param1 type?))
		(= temp2 (param1 message?))
		(cond 
			((and script (script handleEvent: param1)) 1)
			((& temp1 $0040)
				(= temp0 temp2)
				(if (and (== temp0 0) (& temp1 $0004))
					(param1 claimed?)
					(return)
				)
				(if
					(and
						(& temp1 $0004)
						(== temp0 (gUser prevDir?))
						(IsObject mover)
					)
					(= temp0 0)
				)
				(gUser prevDir: temp0)
				(self setDirection: temp0)
				(param1 claimed: 1)
			)
			((& temp1 $4000)
				(if (& temp1 $1000)
					(switch gEgoUseObstacles
						(0
							(self setMotion: MoveTo (param1 x?) (+ (param1 y?) z))
						)
						(1
							(self
								setMotion: PolyPath (param1 x?) (+ (param1 y?) z)
							)
						)
						(2
							(self
								setMotion: PolyPath (param1 x?) (+ (param1 y?) z) 0 0
							)
						)
					)
					(gUser prevDir: 0)
					(param1 claimed: 1)
				else
					(super handleEvent: param1)
				)
			)
			(else (super handleEvent: param1))
		)
		(param1 claimed?)
	)
	
	(method (facingMe)
		(return 1)
	)
	
	(method (get param1 &tmp temp0)
		(= temp0 0)
		(while (< temp0 argc)
			((gInv at: [param1 temp0]) moveTo: self)
			(++ temp0)
		)
	)
	
	(method (put param1 param2 &tmp temp0)
		(if (self has: param1)
			(= temp0 (gInv at: param1))
			(temp0 moveTo: (if (== argc 1) -1 else param2))
			(if
			(and gIconBar (== (gIconBar curInvIcon?) temp0))
				(gIconBar
					curInvIcon: 0
					disable: ((gIconBar useIconItem?) cursor: 999 yourself:)
				)
			)
		)
	)
	
	(method (has param1 &tmp temp0)
		(= temp0 (gInv at: param1))
		(if temp0 ((= temp0 (gInv at: param1)) ownedBy: self))
	)
)
