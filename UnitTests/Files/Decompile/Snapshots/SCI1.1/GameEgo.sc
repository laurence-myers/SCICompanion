;;; Sierra Script 1.0 - (do not remove this comment)
(script# 18)
(include sci.sh)
(use Main)
(use Smopper)
(use Ego)
(use Cycle)
(use System)


(class GameEgo of Ego
	(properties
		x 0
		y 0
		z 0
		heading 0
		noun 22
		_case 0
		modNum 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		sightAngle 26505
		actions 0
		onMeCheck 26505
		state 2
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
	
	(method (handleEvent param1 &tmp temp0 temp1 temp2)
		(= temp1 (param1 type?))
		(= temp2 (param1 message?))
		(return
			(cond 
				((and script (script handleEvent: param1)) 1)
				((& temp1 $0040) (return 0))
				(else (super handleEvent: param1 &rest))
			)
		)
	)
	
	(method (doVerb param1)
		(switch param1
			(1
				(gMessager say: 22 1 0 (Random 1 2) 0 0)
			)
			(4
				(gMessager say: 22 4 0 (Random 1 2) 0 0)
			)
			(2
				(gMessager say: 22 2 0 (Random 1 2) 0 0)
			)
			(else 
				(super doVerb: param1 &rest)
			)
		)
	)
	
	(method (setSpeed param1)
		(= gGEgoMoveSpeed (super setSpeed: param1))
	)
)

(class FiddleStopWalk of Smopper
	(properties
		client 0
		caller 0
		cycleDir 1
		cycleCnt 0
		completed 0
		vInMotion 0
		vStopped 0
		vSlow 0
		vStart 0
		stopState 0
		useSlow 0
		cSpeed 0
		oldSpeed 0
		newCel 0
		tempMover 0
		lCel 0
		ticks 1200
		curTicks -1
		lastTicks 0
		oldControl 0
		oldCycSpeed 0
	)
	
	(method (doit &tmp [temp0 2])
		(if (client isStopped:)
			(cond 
				((!= (gEgo loop?) 8)
					(if (IsOneOf vInMotion 0 1)
						(= oldCycSpeed gGEgoMoveSpeed)
						(cond 
							((== vInMotion 1) (= vSlow 3))
							((IsOneOf (gEgo loop?) 2 4 5) (= vSlow (if (Random 0 1) 12 else 2)))
							(else (= vSlow 12))
						)
						(= curTicks ticks)
					else
						(= vSlow 0)
					)
					(super doit: &rest)
				)
				(
					(and
						(== (gEgo loop?) 8)
						(!= curTicks -1)
						(<= (-= curTicks (Abs (- gGameTime lastTicks))) 0)
					)
					(= curTicks -1)
					(super doit: &rest)
				)
				(
					(and
						(== curTicks -1)
						(not (gRoom script?))
						(not (gEgo script?))
						(gUser canControl:)
						(== (gEgo view?) 0)
						(== (gEgo loop?) (- (NumLoops gEgo) 1))
					)
					(= curTicks ticks)
					(= lCel (gEgo cel?))
					(= oldCycSpeed (gEgo cycleSpeed?))
					(if (IsOneOf (gEgo cel?) 2 4 5)
						(gEgo view: (if (Random 0 1) 10 else 2))
					else
						(gEgo view: 10)
					)
					(gEgo loop: lCel cel: 0 cycleSpeed: 15 setCycle: End self)
				)
				(else (super doit: &rest))
			)
		else
			(super doit: &rest)
		)
		(= lastTicks gGameTime)
	)
	
	(method (cue)
		(gEgo
			view: 0
			cel: lCel
			cycleSpeed: oldCycSpeed
			cycler: self
		)
		(gEgo loop: (- (NumLoops gEgo) 1))
		(self client: gEgo)
	)
)
