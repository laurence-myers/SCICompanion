;;; Sierra Script 1.0 - (do not remove this comment)
(script# 928)
(include sci.sh)
(use Main)
(use Print)
(use Sync)
(use RandCycle)
(use Cycle)
(use Actor)
(use System)


(class Blink of Cycle
	(properties
		client 0
		caller 0
		cycleDir 1
		cycleCnt 0
		completed 0
		waitCount 0
		lastCount 0
		waitMin 0
		waitMax 0
	)
	
	(method (init param1 param2)
		(if argc
			(= waitMin (/ param2 2))
			(= waitMax (+ param2 waitMin))
			(super init: param1)
		else
			(super init:)
		)
	)
	
	(method (doit &tmp blinkNextCel)
		(if waitCount
			(if (> (- gGameTime waitCount) 0)
				(= waitCount 0)
				(self init:)
			)
		else
			(= blinkNextCel (self nextCel:))
			(if
				(or
					(> blinkNextCel (client lastCel:))
					(< blinkNextCel 0)
				)
				(= cycleDir (- cycleDir))
				(self cycleDone:)
			else
				(client cel: blinkNextCel)
			)
		)
	)
	
	(method (cycleDone)
		(if (== cycleDir -1)
			(self init:)
		else
			(= waitCount (+ (Random waitMin waitMax) gGameTime))
		)
	)
)

(class Narrator of Prop
	(properties
		x -1
		y -1
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
		signal 0
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
		caller 0
		disposeWhenDone 2
		ticks 0
		talkWidth 0
		keepWindow 0
		modeless 0
		font 0
		cueVal 0
		initialized 0
		showTitle 0
		color 0
		back 7
		curVolume 0
		saveCursor 0
	)
	
	(method (init &tmp [temp0 3])
		(if
			(or
				(and (& gMessageType $0002) (not modeless))
				(not (HaveMouse))
			)
			(= saveCursor gCursorNumber)
			(gGame setCursor: gWaitCursor 1)
		)
		(= gGameTime (+ gTickOffset (GetTime)))
		(= initialized 1)
	)
	
	(method (doit)
		(if
			(and
				(!= ticks -1)
				(> (- gGameTime ticks) 0)
				(if (& gMessageType $0002) (== (DoAudio 6) -1) else 1)
				(or (not keepWindow) (& gMessageType $0002))
			)
			(self dispose: disposeWhenDone)
			(return 0)
		)
		(return 1)
	)
	
	(method (dispose param1)
		(= ticks -1)
		(if (or (not argc) (== param1 1))
			(cond 
				(modeless
					(gOldKH delete: self)
					(gOldMH delete: self)
					(gTheDoits delete: self)
				)
				((and gFastCast (gFastCast contains: self))
					(gFastCast delete: self)
					(if (gFastCast isEmpty:)
						(gFastCast dispose:)
						(= gFastCast 0)
					)
				)
			)
			(if (& gMessageType $0002) (DoAudio 3))
			(= modNum -1)
			(= initialized 0)
		)
		(if gDialog (gDialog dispose:))
		(if saveCursor
			(if
				(or
					(and (& gMessageType $0002) (not modeless))
					(not (HaveMouse))
				)
				(gGame setCursor: saveCursor)
			)
		else
			(= saveCursor 0)
		)
		(if caller (caller cue: cueVal))
		(= cueVal 0)
		(DisposeClone self)
	)
	
	(method (handleEvent param1)
		(cond 
			((param1 claimed?))
			((== ticks -1) (return 0))
			(else
				(if (not cueVal)
					(switch (param1 type?)
						(256 (= cueVal 0))
						(1
							(= cueVal (& (param1 modifiers?) $0003))
						)
						(4
							(= cueVal (== (param1 message?) 27))
						)
					)
				)
				(if
					(or
						(& (param1 type?) $4101)
						(and
							(& (param1 type?) $0004)
							(IsOneOf (param1 message?) 13 27)
						)
					)
					(param1 claimed: 1)
					(self dispose: disposeWhenDone)
				)
			)
		)
	)
	
	(method (say param1 param2)
		(if gIconBar (gIconBar disable:))
		(if (not initialized) (self init:))
		(= caller (if (and (> argc 1) param2) param2 else 0))
		(if (& gMessageType $0001) (self startText: param1))
		(if (& gMessageType $0002) (self startAudio: param1))
		(cond 
			(modeless
				(gOldMH addToFront: self)
				(gOldKH addToFront: self)
				(gTheDoits add: self)
			)
			((IsObject gFastCast) (gFastCast add: self))
			(else
				(= gFastCast (EventHandler new:))
				(gFastCast name: {fastCast} add: self)
			)
		)
		(= ticks (+ ticks 60 gGameTime))
		(return 1)
	)
	
	(method (startText param1 &tmp temp0)
		(= temp0 (StrLen param1))
		(= ticks (Max 240 (* gTextReadSpeed 2 temp0)))
		(if gDialog (gDialog dispose:))
		(self display: param1)
		(return temp0)
	)
	
	(method (display param1 &tmp theTalkWidth newGWindow [temp2 500])
		(if (> (+ x talkWidth) 318)
			(= theTalkWidth (- 318 x))
		else
			(= theTalkWidth talkWidth)
		)
		(= newGWindow (gWindow new:))
		(newGWindow color: color back: back)
		(if showTitle (Print addTitle: name))
		(Print
			window: newGWindow
			posn: x y
			font: font
			width: theTalkWidth
			modeless: 1
		)
		(if (& gMessageType $0002)
			(Message
				0
				(WordAt param1 0)
				(WordAt param1 1)
				(WordAt param1 2)
				(WordAt param1 3)
				(WordAt param1 4)
				@temp2
			)
			(Print addText: @temp2)
		else
			(Print addText: param1)
		)
		(Print init:)
	)
	
	(method (startAudio param1 &tmp temp0 temp1 temp2 temp3 temp4)
		(= temp0 (WordAt param1 0))
		(= temp1 (WordAt param1 1))
		(= temp2 (WordAt param1 2))
		(= temp3 (WordAt param1 3))
		(= temp4 (WordAt param1 4))
		(if (ResCheck 146 temp0 temp1 temp2 temp3 temp4)
			(= ticks (DoAudio 2 temp0 temp1 temp2 temp3 temp4))
		)
	)
)

(class Talker of Narrator
	(properties
		x -1
		y -1
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
		signal 0
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
		caller 0
		disposeWhenDone 2
		ticks 0
		talkWidth 318
		keepWindow 0
		modeless 0
		font 0
		cueVal 0
		initialized 0
		showTitle 0
		color 0
		back 7
		curVolume 0
		saveCursor 0
		bust 0
		eyes 0
		mouth 0
		viewInPrint 0
		textX 0
		textY 0
		useFrame 0
		blinkSpeed 100
	)
	
	(method (init theBust theEyes theMouth)
		(if argc
			(= bust theBust)
			(if (> argc 1)
				(= eyes theEyes)
				(if (> argc 2) (= mouth theMouth))
			)
		)
		(self setSize:)
		(super init:)
	)
	
	(method (doit)
		(if (and (super doit:) mouth) (self cycle: mouth))
		(if eyes (self cycle: eyes))
	)
	
	(method (dispose param1)
		(if (and mouth underBits)
			(mouth cel: 0)
			(DrawCel
				(mouth view?)
				(mouth loop?)
				0
				(+ (mouth nsLeft?) nsLeft)
				(+ (mouth nsTop?) nsTop)
				-1
			)
		)
		(if (and mouth (mouth cycler?))
			(if ((mouth cycler?) respondsTo: 145)
				((mouth cycler?) cue:)
			)
			(mouth setCycle: 0)
		)
		(if (or (not argc) (== param1 1))
			(if (and eyes underBits)
				(eyes setCycle: 0 cel: 0)
				(DrawCel
					(eyes view?)
					(eyes loop?)
					0
					(+ (eyes nsLeft?) nsLeft)
					(+ (eyes nsTop?) nsTop)
					-1
				)
			)
			(self hide:)
		)
		(super dispose: param1)
	)
	
	(method (hide)
		(Graph 8 underBits)
		(= underBits 0)
		(Graph 13 nsTop nsLeft nsBottom nsRight)
		(if gIconBar (gIconBar enable:))
	)
	
	(method (show &tmp temp0)
		(if (not underBits)
			(= underBits (Graph 7 nsTop nsLeft nsBottom nsRight 1))
		)
		(= temp0 (PicNotValid))
		(PicNotValid 1)
		(if bust
			(DrawCel
				(bust view?)
				(bust loop?)
				(bust cel?)
				(+ (bust nsLeft?) nsLeft)
				(+ (bust nsTop?) nsTop)
				-1
			)
		)
		(if eyes
			(DrawCel
				(eyes view?)
				(eyes loop?)
				(eyes cel?)
				(+ (eyes nsLeft?) nsLeft)
				(+ (eyes nsTop?) nsTop)
				-1
			)
		)
		(if mouth
			(DrawCel
				(mouth view?)
				(mouth loop?)
				(mouth cel?)
				(+ (mouth nsLeft?) nsLeft)
				(+ (mouth nsTop?) nsTop)
				-1
			)
		)
		(DrawCel view loop cel nsLeft nsTop -1)
		(Graph 12 nsTop nsLeft nsBottom nsRight 1)
		(PicNotValid temp0)
	)
	
	(method (say)
		(if (and (> view 0) (not underBits)) (self init:))
		(super say: &rest)
	)
	
	(method (startText &tmp temp0)
		(if (not viewInPrint) (self show:))
		(= temp0 (super startText: &rest))
		(if mouth (mouth setCycle: RandCycle (* 4 temp0) 0 1))
		(if (and eyes (not (eyes cycler?)))
			(eyes setCycle: Blink blinkSpeed)
		)
	)
	
	(method (display param1 &tmp temp0 theTalkWidth temp2 newGWindow [temp4 500])
		(= newGWindow (gWindow new:))
		(newGWindow color: color back: back)
		(if viewInPrint
			(= temp0 (if useFrame loop else (bust loop?)))
			(if showTitle (Print addTitle: name))
			(Print
				window: newGWindow
				posn: x y
				modeless: 1
				font: font
				addText: param1
				addIcon: view temp0 cel 0 0
				init:
			)
		else
			(if (not (+ textX textY))
				(= textX (+ (- nsRight nsLeft) 5))
			)
			(= temp2 (+ nsLeft textX))
			(if (> (+ temp2 talkWidth) 318)
				(= theTalkWidth (- 318 temp2))
			else
				(= theTalkWidth talkWidth)
			)
			(if showTitle (Print addTitle: name))
			(Print
				window: newGWindow
				posn: (+ x textX) (+ y textY)
				modeless: 1
				font: font
				width: theTalkWidth
			)
			(if (& gMessageType $0002)
				(Message
					0
					(WordAt param1 0)
					(WordAt param1 1)
					(WordAt param1 2)
					(WordAt param1 3)
					(WordAt param1 4)
					@temp4
				)
				(Print addText: @temp4)
			else
				(Print addText: param1)
			)
			(Print init:)
		)
	)
	
	(method (startAudio param1 &tmp temp0 temp1 temp2 temp3 temp4 temp5)
		(self show:)
		(if mouth
			(= temp0 (WordAt param1 0))
			(= temp1 (WordAt param1 1))
			(= temp2 (WordAt param1 2))
			(= temp3 (WordAt param1 3))
			(= temp4 (WordAt param1 4))
			(if (ResCheck 147 temp0 temp1 temp2 temp3 temp4)
				(mouth setCycle: MouthSync temp0 temp1 temp2 temp3 temp4)
				(= temp5 (super startAudio: param1))
			else
				(= temp5 (super startAudio: param1))
				(mouth setCycle: RandCycle temp5 0)
			)
		else
			(= temp5 (super startAudio: param1))
		)
		(if (and eyes (not (eyes cycler?)))
			(eyes setCycle: Blink blinkSpeed)
		)
	)
	
	(method (cycle param1 &tmp temp0 [temp1 100])
		(if (and param1 (param1 cycler?))
			(= temp0 (param1 cel?))
			((param1 cycler?) doit:)
			(if (!= temp0 (param1 cel?))
				(DrawCel
					(param1 view?)
					(param1 loop?)
					(param1 cel?)
					(+ (param1 nsLeft?) nsLeft)
					(+ (param1 nsTop?) nsTop)
					-1
				)
				(param1
					nsRight:
						(+
							(param1 nsLeft?)
							(CelWide (param1 view?) (param1 loop?) (param1 cel?))
						)
				)
				(param1
					nsBottom:
						(+
							(param1 nsTop?)
							(CelHigh (param1 view?) (param1 loop?) (param1 cel?))
						)
				)
				(Graph
					12
					(+ (param1 nsTop?) nsTop)
					(+ (param1 nsLeft?) nsLeft)
					(+ (param1 nsBottom?) nsTop)
					(+ (param1 nsRight?) nsLeft)
					1
				)
			)
		)
	)
	
	(method (setSize)
		(= nsLeft x)
		(= nsTop y)
		(= nsRight
			(+
				nsLeft
				(Max
					(if view (CelWide view loop cel) else 0)
					(if (IsObject bust)
						(+
							(bust nsLeft?)
							(CelWide (bust view?) (bust loop?) (bust cel?))
						)
					else
						0
					)
					(if (IsObject eyes)
						(+
							(eyes nsLeft?)
							(CelWide (eyes view?) (eyes loop?) (eyes cel?))
						)
					else
						0
					)
					(if (IsObject mouth)
						(+
							(mouth nsLeft?)
							(CelWide (mouth view?) (mouth loop?) (mouth cel?))
						)
					else
						0
					)
				)
			)
		)
		(= nsBottom
			(+
				nsTop
				(Max
					(if view (CelHigh view loop cel) else 0)
					(if (IsObject bust)
						(+
							(bust nsTop?)
							(CelHigh (bust view?) (bust loop?) (bust cel?))
						)
					else
						0
					)
					(if (IsObject eyes)
						(+
							(eyes nsTop?)
							(CelHigh (eyes view?) (eyes loop?) (eyes cel?))
						)
					else
						0
					)
					(if (IsObject mouth)
						(+
							(mouth nsTop?)
							(CelHigh (mouth view?) (mouth loop?) (mouth cel?))
						)
					else
						0
					)
				)
			)
		)
	)
)
