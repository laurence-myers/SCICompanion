;;; Sierra Script 1.0 - (do not remove this comment)
(script# 0)
(include sci.sh)
(use ColorInit)
(use GameEgo)
(use SpeakWindow)
(use Print)
(use Messager)
(use Talker)
(use PseudoMouse)
(use BorderWindow)
(use IconItem)
(use Polygon)
(use Grooper)
(use Sound)
(use Game)
(use User)
(use System)

(public
	SQ5 0
	Btest 1
	Bset 2
	Bclear 3
	RestorePreviousHandsOn 4
	IsObjectOnControl 5
	SetUpEgo 6
	AddToScore 7
	AimToward 8
	Die 9
	ScoreFlag 10
	HideStatus 11
	DebugPrint 12
	AddPolygonsToRoom 13
	CreateNewPolygon 14
)

(local
	gEgo
	gGame
	gRoom
	global3
	gQuitGame
	gCast
	gRegions
	gTimers
	gSounds
	gInv
	gAddToPics
	gRoomNumber
	gPreviousRoomNumber
	gNewRoomNumber
	gDebugOnNextRoom
	gScore
	gMaxScore
	gTextCode
	gCuees
	gCursorNumber
	gNormalCursor =  999
	gWaitCursor =  997
	gFont =  1
	gSmallFont =  4
	gPEvent
	gDialog
	gBigFont =  1
	gVersion
	gSaveDir
	gPicAngle
	gFeatures
	gUseSortedFeatures
	gPicNumber =  -1
	gDoMotionCue
	gWindow
	global35
	global36
	gOldPort
	gDebugFilename
	global39
	global40
	global41
	global42
	global43
	global44
	global45
	global46
	global47
	global48
	global49
	global50
	global51
	global52
	global53
	global54
	global55
	global56
	global57
	global58
	gGameControls
	gFeatureInit
	gDoVerbCode
	gApproachCode
	gEgoUseObstacles =  1
	gIconBar
	gPEventX
	gPEventY
	gOldKH
	gOldMH
	gOldDH
	gPseudoMouse
	gTheDoits
	gEatTheMice =  60
	gUser
	gSyncBias
	gTheSync
	global76
	gFastCast
	gInputFont
	gTickOffset
	gGameTime
	gNarrator
	gMessageType =  1
	gMessager
	gPrints
	gWalkHandler
	gTextReadSpeed =  2
	gAltPolyList
	gColorDepth
	gPolyphony
	gStopGroop
	global91
	gCurrentIcon
	gGUserCanControl
	gGUserCanInput
	gCheckedIcons
	gState
	gNewSpeakWindow
	gWindow2
	gDeathReason
	gMusic1
	gDongle =  1234
	gMusic2
	gCurrentTalkerNumber
	gGEgoMoveSpeed
	gColorWindowForeground
	gColorWindowBackground
	gLowlightColor
	gDefaultEgoView
	gRegister
	gFlags
	global111
	global112
	global113
	global114
	global115
	global116
	global117
	global118
	global119
	global120
	global121
	global122
	global123
	gEdgeDistance =  10
	gDebugOut
)
(procedure (Btest param1)
	(return (& [gFlags (/ param1 16)] (>> $8000 (mod param1 16))))
)

(procedure (Bset param1 &tmp temp0)
	(= temp0 (Btest param1))
	(|= [gFlags (/ param1 16)] (>> $8000 (mod param1 16)))
	(return temp0)
)

(procedure (Bclear param1 &tmp temp0)
	(= temp0 (Btest param1))
	(&= [gFlags (/ param1 16)]
		(~ (>> $8000 (mod param1 16)))
	)
	(return temp0)
)

(procedure (RestorePreviousHandsOn &tmp temp0)
	(gUser
		canControl: gGUserCanControl
		canInput: gGUserCanInput
	)
	(= temp0 0)
	(while (< temp0 8)
		(if (& gCheckedIcons (>> $8000 temp0))
			(gIconBar disable: temp0)
		)
		(++ temp0)
	)
)

(procedure (IsObjectOnControl param1 param2)
	(if (& (param1 onControl: 1) param2) (return 1) else 0)
)

(procedure (SetUpEgo param1 param2)
	(if (and (> argc 0) (!= param1 -1))
		(gEgo view: param1)
		(if (and (> argc 1) (!= param2 -1))
			(gEgo loop: param2)
		)
	else
		(gEgo view: gDefaultEgoView)
		(if (and (> argc 1) (!= param2 -1))
			(gEgo loop: param2)
		)
	)
	(if (gEgo looper?) ((gEgo looper?) dispose:))
	(gEgo
		setStep: 5 2
		illegalBits: 0
		ignoreActors: 0
		setSpeed: gGEgoMoveSpeed
		heading:
			(switch (gEgo loop?)
				(0 90)
				(1 270)
				(2 180)
				(3 0)
				(4 135)
				(5 225)
				(6 45)
				(7 315)
			)
	)
	(gEgo
		setLoop: -1
		setLoop: stopGroop
		setPri: -1
		setMotion: 0
		state: (| (gEgo state?) $0002)
	)
)

(procedure (AddToScore theGScore)
	(+= gScore theGScore)
	(statusLineCode doit:)
	(rm0Sound
		priority: 15
		number: 1000
		loop: 1
		flags: 1
		play:
	)
)

(procedure (AimToward param1 param2 param3 param4 &tmp temp0 temp1 temp2 temp3 temp4)
	(= temp3 0)
	(= temp4 0)
	(if (IsObject param2)
		(= temp1 (param2 x?))
		(= temp2 (param2 y?))
		(if (> argc 2)
			(if (IsObject param3)
				(= temp3 param3)
			else
				(= temp4 param3)
			)
			(if (== argc 4) (= temp3 param4))
		)
	else
		(= temp1 param2)
		(= temp2 param3)
		(if (== argc 4) (= temp3 param4))
	)
	(if temp4 (AimToward param2 param1))
	(= temp0
		(GetAngle (param1 x?) (param1 y?) temp1 temp2)
	)
	(param1
		setHeading: temp0 (if (IsObject temp3) temp3 else 0)
	)
)

(procedure (Die theGDeathReason)
	(if (not argc)
		(= gDeathReason 1)
	else
		(= gDeathReason theGDeathReason)
	)
	(gRoom newRoom: 20)
)

(procedure (ScoreFlag param1 param2)
	(if (not (Btest param1))
		(AddToScore param2)
		(Bset param1)
	)
)

(procedure (HideStatus &tmp temp0)
	(= temp0 (GetPort))
	(SetPort -1)
	(Graph 11 0 0 10 320 1 0 -1 -1)
	(Graph 12 0 0 10 320 1)
	(SetPort temp0)
)

(procedure (DebugPrint)
	(if gDebugOut (gDebugOut debugPrint: &rest))
)

(procedure (AddPolygonsToRoom param1 &tmp temp0)
	(if (u< param1 100)
		(Prints {polyBuffer is not a pointer. Polygon ignored.})
	else
		(= temp0 (Memory 5 param1))
		(+= param1 2)
		(while temp0
			(gRoom
				addObstacle:
					(if (== temp0 1)
						(localproc_0403 param1)
					else
						(localproc_0403 param1 @param1)
					)
			)
			(-- temp0)
		)
	)
)

(procedure (CreateNewPolygon param1 &tmp temp0)
	(if (u< param1 100)
		(Prints {polyBuffer is not a pointer. Polygon ignored.})
		(return 0)
	else
		(= temp0 (Memory 5 param1))
		(+= param1 2)
		(return (localproc_0403 param1 &rest))
	)
)

(procedure (localproc_0403 param1 param2 &tmp newPolygon temp1)
	(= newPolygon (Polygon new:))
	(= temp1 (Memory 5 (+ param1 2)))
	(newPolygon
		dynamic: 0
		type: (Memory 5 param1)
		size: temp1
		points: (+ param1 4)
	)
	(if (> argc 1)
		(Memory 6 param2 (+ param1 4 (* 4 temp1)))
	)
	(return newPolygon)
)

(instance rm0Sound of Sound
	(properties
		priority 15
	)
)

(instance music1 of Sound
	(properties
		flags 1
	)
)

(instance music2 of Sound
	(properties
		flags 1
	)
)

(instance stopGroop of Grooper
	(properties)
)

(instance egoStopWalk of FiddleStopWalk
	(properties)
)

(instance ego of GameEgo
	(properties)
)

(instance statusLineCode of Code
	(properties)
	
	(method (doit &tmp [temp0 50] [temp50 50] temp100)
		(= temp100 (GetPort))
		(SetPort -1)
		(Graph 11 0 0 10 320 1 5 -1 -1)
		(Graph 12 0 0 10 320 1)
		(Message 0 0 29 0 0 1 @temp0)
		(Format @temp50 {%s %d} @temp0 gScore)
		(Display @temp50 dsCOORD 4 0 dsFONT gFont dsCOLOR 6)
		(Display @temp50 dsCOORD 6 2 dsFONT gFont dsCOLOR 4)
		(Display @temp50 dsCOORD 5 1 dsFONT gFont dsCOLOR 0)
		(Graph 4 0 0 0 319 7 -1 -1)
		(Graph 4 0 0 9 0 6 -1 -1)
		(Graph 4 9 0 9 319 4 -1 -1)
		(Graph 4 0 319 9 319 3 -1 -1)
		(Graph 12 0 0 10 319 1)
		(SetPort temp100)
	)
)

(instance templateIconBar of IconBar
	(properties)
	
	(method (show)
		(if (IsObject curInvIcon) (curInvIcon loop: 2))
		(super show:)
		(if (IsObject curInvIcon) (curInvIcon loop: 1))
	)
	
	(method (hide)
		(super hide: &rest)
		(gGame setCursor: gCursorNumber 1)
	)
	
	(method (noClickHelp &tmp temp0 temp1 temp2 temp3 gWindowEraseOnly)
		(= temp2 0)
		(= temp1 temp2)
		(= temp3 (GetPort))
		(= gWindowEraseOnly (gWindow eraseOnly?))
		(gWindow eraseOnly: 1)
		(while
		(not ((= temp0 ((gUser curEvent?) new:)) type?))
			(if (not (self isMemberOf: IconBar)) (temp0 localize:))
			(= temp2 (self firstTrue: 226 temp0))
			(cond 
				(temp2
					(if
						(and
							(!= (= temp2 (self firstTrue: 226 temp0)) temp1)
							(temp2 helpVerb?)
						)
						(= temp1 temp2)
						(if gDialog (gDialog dispose:))
						(Print
							font: gFont
							width: 250
							addText: (temp2 noun?) (temp2 helpVerb?) 0 1 0 0 (temp2 modNum?)
							modeless: 1
							init:
						)
						(Animate (gCast elements?) 0)
						(SetPort temp3)
					)
				)
				(gDialog (gDialog dispose:) (Animate (gCast elements?) 0))
				(else (= temp1 0))
			)
			(temp0 dispose:)
		)
		(gWindow eraseOnly: gWindowEraseOnly)
		(gGame setCursor: 999 1)
		(if gDialog
			(gDialog dispose:)
			(Animate (gCast elements?) 0)
		)
		(SetPort temp3)
		(if (not (helpIconItem onMe: temp0))
			(self dispatchEvent: temp0)
		)
	)
)

(class SQ5 of Game
	(properties
		script 0
		printLang 1
		_detailLevel 3
		panelObj 0
		panelSelector 0
		handsOffCode 0
		handsOnCode 0
	)
	
	(method (init &tmp [temp0 7] temp7)
		((ScriptID 15 0) init:)
		(super init:)
		(= gEgo ego)
		(User alterEgo: gEgo canControl: 0 canInput: 0)
		(= gMessageType 1)
		(= gUseSortedFeatures 1)
		(= gPolyphony (DoSound 3))
		(= gMaxScore 5000)
		(= gFont 1605)
		(= gGEgoMoveSpeed 6)
		(= gEatTheMice 30)
		(= gTextReadSpeed 2)
		(= gColorDepth (Graph 2))
		(= gStopGroop stopGroop)
		(= gPseudoMouse PseudoMouse)
		(gEgo setLoop: gStopGroop)
		(TextFonts 1605 1605 1605 1605 1605 0)
		(TextColors 0 15 26 31 34 52 63)
		(= gVersion {x.yyy.zzz})
		(= temp7 (FileIO 0 {version} 1))
		(FileIO 5 gVersion 11 temp7)
		(FileIO 1 temp7)
		(ColorInit)
		(DisposeScript 12)
		(= gNarrator templateNarrator)
		(= gWindow mainWindow)
		(= gWindow2 mainWindow)
		(= gMessager testMessager)
		(= gNewSpeakWindow (SpeakWindow new:))
		(gWindow
			color: gColorWindowForeground
			back: gColorWindowBackground
		)
		(gGame setCursor: gCursorNumber 1 304 172 detailLevel: 3)
		(= gMusic1 music1)
		(gMusic1 owner: self flags: 1 init:)
		(= gMusic2 music2)
		(gMusic2 owner: self flags: 1 init:)
		(= gIconBar templateIconBar)
		(gIconBar
			add: icon0 icon1 icon2 icon3 icon4 icon6 icon7 icon8 icon9
			eachElementDo: 110
			eachElementDo: 219 0
			eachElementDo: 220 5
			curIcon: icon0
			useIconItem: icon6
			helpIconItem: icon9
			walkIconItem: icon0
			disable: 5
			state: 3072
			disable:
		)
		(= gNormalCursor 999)
		(= gWaitCursor 996)
		(= gDoVerbCode lb2DoVerbCode)
		(= gFeatureInit lb2FtrInit)
		(= gApproachCode lb2ApproachCode)
	)
	
	(method (doit)
		(if (GameIsRestarting)
			(if (IsOneOf gRoomNumber 100)
				(HideStatus)
			else
				(statusLineCode doit:)
			)
			(= gColorDepth (Graph 2))
		)
		(super doit: &rest)
	)
	
	(method (play &tmp temp0 temp1 temp2)
		(= gGame self)
		(= gSaveDir (GetSaveDir))
		(if (not (GameIsRestarting)) (GetCWD gSaveDir))
		(self setCursor: gWaitCursor 1 init:)
		(= temp2 100)
		(if
		(and (not (GameIsRestarting)) (FileIO 10 {sdebug.txt}))
			(= gDebugOut (ScriptID 14 0))
			(DebugPrint {Debugger enabled})
			(= temp1 (gDebugOut init: {sdebug.txt}))
			(if (!= temp1 -1)
				(= temp2 temp1)
				(gGame handsOn:)
				(DebugPrint {Starting in room %d} temp2)
			)
		)
		(self newRoom: temp2)
		(while (not gQuitGame)
			(self doit:)
		)
	)
	
	(method (startRoom param1 &tmp [temp0 4])
		(if (IsOneOf param1 100)
			(HideStatus)
		else
			(statusLineCode doit:)
		)
		(if gPseudoMouse (gPseudoMouse stop:))
		((ScriptID 11) doit: param1)
		(super startRoom: param1)
	)
	
	(method (restart &tmp temp0 temp1)
		(= temp1 ((gIconBar curIcon?) cursor?))
		(gGame setCursor: 999)
		(= temp0
			(Print
				font: gFont
				width: 75
				window: gWindow
				mode: 1
				addText: 20 1 0 1 0 0 0
				addColorButton: 1 20 1 0 2 0 40 0
				addColorButton: 0 20 1 0 3 0 50 0
				init:
			)
		)
		(if temp0
			(super restart: &rest)
		else
			(gGame setCursor: temp1)
		)
	)
	
	(method (restore &tmp [temp0 2])
		(super restore: &rest)
		(gGame setCursor: ((gIconBar curIcon?) cursor?))
	)
	
	(method (save)
		(super save: &rest)
		(gGame setCursor: ((gIconBar curIcon?) cursor?))
	)
	
	(method (handleEvent param1 &tmp theGCursorNumber)
		(super handleEvent: param1)
		(if (param1 claimed?) (return 1))
		(return
			(switch (param1 type?)
				(4
					(switch (param1 message?)
						(9
							(if (not (& ((gIconBar at: 6) signal?) $0004))
								(if gFastCast (return gFastCast))
								(= theGCursorNumber gCursorNumber)
								(gInv showSelf: gEgo)
								(gGame setCursor: theGCursorNumber 1)
								(param1 claimed: 1)
							)
						)
						(17
							(if (not (& ((gIconBar at: 7) signal?) $0004))
								(gGame quitGame:)
								(param1 claimed: 1)
							)
						)
						(3
							(if (not (& ((gIconBar at: 7) signal?) $0004))
								(= theGCursorNumber ((gIconBar curIcon?) cursor?))
								((ScriptID 24 0) doit:)
								(gGameControls dispose:)
								(gGame setCursor: theGCursorNumber 1)
							)
						)
						(15360
							(cond 
								((gGame masterVolume:) (gGame masterVolume: 0))
								((> gPolyphony 1) (gGame masterVolume: 15))
								(else (gGame masterVolume: 1))
							)
							(param1 claimed: 1)
						)
						(16128
							(if (not (& ((gIconBar at: 7) signal?) $0004))
								(if gFastCast (return gFastCast))
								(= theGCursorNumber gCursorNumber)
								(gGame save:)
								(gGame setCursor: theGCursorNumber 1)
								(param1 claimed: 1)
							)
						)
						(16640
							(if (not (& ((gIconBar at: 7) signal?) $0004))
								(if gFastCast (return gFastCast))
								(= theGCursorNumber gCursorNumber)
								(gGame restore:)
								(gGame setCursor: theGCursorNumber 1)
								(param1 claimed: 1)
							)
						)
						(43
							(if (gUser controls?)
								(= gGEgoMoveSpeed (gEgo moveSpeed?))
								(= gGEgoMoveSpeed (Max 0 (-- gGEgoMoveSpeed)))
								(gEgo setSpeed: gGEgoMoveSpeed)
							)
						)
						(45
							(if (gUser controls?)
								(= gGEgoMoveSpeed (gEgo moveSpeed?))
								(gEgo setSpeed: (++ gGEgoMoveSpeed))
							)
						)
						(61
							(if (gUser controls?) (gEgo setSpeed: 6))
						)
						(12032
							(Print
								addText: {Version number:} 0 0
								addText: gVersion 0 14
								init:
							)
						)
						(8192 ((ScriptID 10 0) init:))
						(else  (param1 claimed: 0))
					)
				)
			)
		)
	)
	
	(method (setCursor theGCursorNumber_2 param2 param3 param4 &tmp theGCursorNumber)
		(= theGCursorNumber gCursorNumber)
		(if argc
			(if (IsObject theGCursorNumber_2)
				(= gCursorNumber theGCursorNumber_2)
				(gCursorNumber init:)
			else
				(= gCursorNumber theGCursorNumber_2)
				(SetCursor gCursorNumber 0 0)
			)
		)
		(if (and (> argc 1) (not param2)) (SetCursor 996 0 0))
		(if (> argc 2) (SetCursor param3 param4))
		(return theGCursorNumber)
	)
	
	(method (quitGame &tmp temp0 temp1)
		(= temp1 ((gIconBar curIcon?) cursor?))
		(gGame setCursor: 999)
		(= temp0
			(Print
				font: gFont
				width: 75
				mode: 1
				addText: 19 1 0 1 0 0 0
				addColorButton: 1 19 1 0 2 0 25 0
				addColorButton: 0 19 1 0 3 0 35 0
				init:
			)
		)
		(if temp0
			(Print addText: 19 1 0 4 0 0 0 init:)
			(super quitGame: &rest)
		else
			(gGame setCursor: temp1)
		)
	)
	
	(method (pragmaFail)
		(if (User canControl:)
			(switch ((gUser curEvent?) message?)
				(4
					(gMessager say: 0 4 0 (Random 1 2) 0 0)
				)
				(2
					(gMessager say: 0 2 0 (Random 1 2) 0 0)
				)
				(else 
					(if
					(not (IsOneOf ((gUser curEvent?) message?) 1))
						(gMessager say: 0 7 0 (Random 2 3) 0 0)
					)
				)
			)
		)
	)
	
	(method (handsOff)
		(if (not gCurrentIcon)
			(= gCurrentIcon (gIconBar curIcon?))
		)
		(= gGUserCanControl (gUser canControl:))
		(= gGUserCanInput (gUser canInput:))
		(gUser canControl: 0 canInput: 0)
		(gEgo setMotion: 0)
		(= gCheckedIcons 0)
		(gIconBar eachElementDo: 96 checkIcon)
		(gIconBar curIcon: (gIconBar at: 7))
		(gIconBar disable:)
		(gIconBar disable: 0 1 2 3 4 5 6 7)
		(gGame setCursor: 996)
	)
	
	(method (handsOn param1)
		(gIconBar enable:)
		(gUser canControl: 1 canInput: 1)
		(gIconBar enable: 0 1 2 3 5 6 7)
		(gIconBar disable: 4)
		(if (and argc param1) (RestorePreviousHandsOn))
		(if (not (gIconBar curInvIcon?)) (gIconBar disable: 5))
		(if gCurrentIcon
			(gIconBar curIcon: gCurrentIcon)
			(gGame setCursor: (gCurrentIcon cursor?))
			(= gCurrentIcon 0)
			(if
				(and
					(== (gIconBar curIcon?) (gIconBar at: 5))
					(not (gIconBar curInvIcon?))
				)
				(gIconBar advanceCurIcon:)
			)
		)
		(gGame setCursor: ((gIconBar curIcon?) cursor?) 1)
		(= gCursorNumber ((gIconBar curIcon?) cursor?))
	)
	
	(method (showAbout)
		((ScriptID 13 0) doit:)
		(DisposeScript 13)
	)
	
	(method (showControls &tmp temp0)
		(= temp0 ((gIconBar curIcon?) cursor?))
		((ScriptID 24 0) doit:)
		(gGameControls dispose:)
		(gGame setCursor: temp0 1)
	)
)

(instance icon0 of IconItem
	(properties
		view 990
		loop 0
		cel 0
		cursor 980
		type 20480
		message 3
		signal 65
		maskView 990
		maskLoop 13
		noun 28
		helpVerb 5
	)
	
	(method (init)
		(= lowlightColor gLowlightColor)
		(super init:)
	)
	
	(method (select &tmp temp0)
		(if (super select: &rest)
			(gIconBar hide:)
			(return 1)
		else
			(return 0)
		)
	)
)

(instance icon1 of IconItem
	(properties
		view 990
		loop 1
		cel 0
		cursor 981
		message 1
		signal 65
		maskView 990
		maskLoop 13
		noun 16
		helpVerb 5
	)
	
	(method (init)
		(= lowlightColor gLowlightColor)
		(super init:)
	)
)

(instance icon2 of IconItem
	(properties
		view 990
		loop 2
		cel 0
		cursor 982
		message 4
		signal 65
		maskView 990
		maskLoop 13
		noun 6
		helpVerb 5
	)
	
	(method (init)
		(= lowlightColor gLowlightColor)
		(super init:)
	)
)

(instance icon3 of IconItem
	(properties
		view 990
		loop 3
		cel 0
		cursor 983
		message 2
		signal 65
		maskView 990
		maskLoop 13
		maskCel 4
		noun 26
		helpVerb 5
	)
	
	(method (init)
		(= lowlightColor gLowlightColor)
		(super init:)
	)
)

(instance icon4 of IconItem
	(properties
		view 990
		loop 10
		cel 0
		cursor 999
		message 0
		signal 65
		maskView 990
		maskLoop 13
		maskCel 4
		helpVerb 5
	)
	
	(method (init)
		(= lowlightColor gLowlightColor)
		(super init:)
	)
)

(instance icon6 of IconItem
	(properties
		view 990
		loop 4
		cel 0
		cursor 999
		message 0
		signal 65
		maskView 990
		maskLoop 13
		maskCel 4
		noun 4
		helpVerb 5
	)
	
	(method (init)
		(= lowlightColor gLowlightColor)
		(super init:)
	)
	
	(method (select param1 &tmp newEvent temp1 gIconBarCurInvIcon temp3 temp4)
		(return
			(cond 
				((& signal $0004) 0)
				((and argc param1 (& signal $0001))
					(= gIconBarCurInvIcon (gIconBar curInvIcon?))
					(if gIconBarCurInvIcon
						(= temp3
							(+
								(/
									(-
										(- nsRight nsLeft)
										(CelWide
											(gIconBarCurInvIcon view?)
											2
											(gIconBarCurInvIcon cel?)
										)
									)
									2
								)
								nsLeft
							)
						)
						(= temp4
							(+
								(gIconBar y?)
								(/
									(-
										(- nsBottom nsTop)
										(CelHigh
											(gIconBarCurInvIcon view?)
											2
											(gIconBarCurInvIcon cel?)
										)
									)
									2
								)
								nsTop
							)
						)
					)
					(= temp1 1)
					(DrawCel view loop temp1 nsLeft nsTop -1)
					(= gIconBarCurInvIcon (gIconBar curInvIcon?))
					(if gIconBarCurInvIcon
						(DrawCel
							((= gIconBarCurInvIcon (gIconBar curInvIcon?)) view?)
							2
							(gIconBarCurInvIcon cel?)
							temp3
							temp4
							-1
						)
					)
					(Graph 12 nsTop nsLeft nsBottom nsRight 1)
					(while (!= ((= newEvent (Event new:)) type?) 2)
						(newEvent localize:)
						(cond 
							((self onMe: newEvent)
								(if (not temp1)
									(= temp1 1)
									(DrawCel view loop temp1 nsLeft nsTop -1)
									(= gIconBarCurInvIcon (gIconBar curInvIcon?))
									(if gIconBarCurInvIcon
										(DrawCel
											((= gIconBarCurInvIcon (gIconBar curInvIcon?)) view?)
											2
											(gIconBarCurInvIcon cel?)
											temp3
											temp4
											-1
										)
									)
									(Graph 12 nsTop nsLeft nsBottom nsRight 1)
								)
							)
							(temp1
								(= temp1 0)
								(DrawCel view loop temp1 nsLeft nsTop -1)
								(= gIconBarCurInvIcon (gIconBar curInvIcon?))
								(if gIconBarCurInvIcon
									(DrawCel
										((= gIconBarCurInvIcon (gIconBar curInvIcon?)) view?)
										2
										(gIconBarCurInvIcon cel?)
										temp3
										temp4
										-1
									)
								)
								(Graph 12 nsTop nsLeft nsBottom nsRight 1)
							)
						)
						(newEvent dispose:)
					)
					(newEvent dispose:)
					(if (== temp1 1)
						(DrawCel view loop 0 nsLeft nsTop -1)
						(= gIconBarCurInvIcon (gIconBar curInvIcon?))
						(if gIconBarCurInvIcon
							(DrawCel
								((= gIconBarCurInvIcon (gIconBar curInvIcon?)) view?)
								2
								(gIconBarCurInvIcon cel?)
								temp3
								temp4
								-1
							)
						)
						(Graph 12 nsTop nsLeft nsBottom nsRight 1)
					)
					temp1
				)
				(else 1)
			)
		)
	)
)

(instance icon7 of IconItem
	(properties
		view 990
		loop 5
		cel 0
		cursor 999
		type 0
		message 0
		signal 67
		maskView 990
		maskLoop 13
		noun 15
		helpVerb 5
	)
	
	(method (init)
		(= lowlightColor gLowlightColor)
		(super init:)
	)
	
	(method (select &tmp theGCursorNumber)
		(if (super select: &rest)
			(gIconBar hide:)
			(= theGCursorNumber gCursorNumber)
			(gInv showSelf: gEgo)
			(gGame setCursor: theGCursorNumber 1)
			(return 1)
		else
			(return 0)
		)
	)
)

(instance icon8 of IconItem
	(properties
		view 990
		loop 7
		cel 0
		cursor 999
		message 7
		signal 67
		maskView 990
		maskLoop 13
		noun 3
		helpVerb 5
	)
	
	(method (init)
		(= lowlightColor gLowlightColor)
		(super init:)
	)
	
	(method (select)
		(if (super select: &rest)
			(gIconBar hide:)
			(gGame showControls:)
			(return 1)
		else
			(return 0)
		)
	)
)

(instance icon9 of IconItem
	(properties
		view 990
		loop 9
		cel 0
		cursor 989
		type 8192
		message 5
		signal 3
		maskView 990
		maskLoop 13
		noun 7
		helpVerb 5
	)
	
	(method (init)
		(= lowlightColor gLowlightColor)
		(if gDialog (gDialog dispose:))
		(super init:)
	)
)

(instance checkIcon of Code
	(properties)
	
	(method (doit param1)
		(if
			(and
				(param1 isKindOf: IconItem)
				(& (param1 signal?) $0004)
			)
			(|= gCheckedIcons (>> $8000 (gIconBar indexOf: param1)))
		)
	)
)

(instance lb2DoVerbCode of Code
	(properties)
	
	(method (doit param1 param2)
		(if (User canControl:)
			(if (== param2 gEgo)
				(if (Message 2 0 22 param1 0 1)
					(gMessager say: 22 param1 0 0 0 0)
				else
					(gMessager say: 22 0 0 (Random 1 2) 0 0)
				)
			else
				(switch param1
					(4
						(gMessager say: 0 4 0 (Random 1 2) 0 0)
					)
					(2
						(gMessager say: 0 2 0 (Random 1 2) 0 0)
					)
					(else 
						(if (not (IsOneOf param1 1))
							(gMessager say: 0 7 0 (Random 2 3) 0 0)
						)
					)
				)
			)
		)
	)
)

(instance lb2FtrInit of Code
	(properties)
	
	(method (doit param1)
		(if (== (param1 sightAngle?) 26505)
			(param1 sightAngle: 90)
		)
		(if (== (param1 actions?) 26505) (param1 actions: 0))
		(if
			(and
				(not (param1 approachX?))
				(not (param1 approachY?))
			)
			(param1 approachX: (param1 x?) approachY: (param1 y?))
		)
	)
)

(instance lb2ApproachCode of Code
	(properties)
	
	(method (doit param1)
		(return
			(switch param1
				(1 1)
				(2 2)
				(3 4)
				(4 8)
				(else  -32768)
			)
		)
	)
)

(instance mainWindow of BorderWindow
	(properties)
)

(instance templateNarrator of Narrator
	(properties)
	
	(method (init)
		(= font gFont)
		(self back: gColorWindowBackground)
		(super init: &rest)
	)
)

(instance testMessager of Messager
	(properties)
	
	(method (findTalker theGCurrentTalkerNumber &tmp temp0)
		(= gCurrentTalkerNumber theGCurrentTalkerNumber)
		(= temp0
			(switch theGCurrentTalkerNumber
				(99 gNarrator)
			)
		)
		(if temp0
			(if (not (temp0 isKindOf: Narrator))
				(Prints {Invalid talker.})
			)
			(return temp0)
		else
			(return (super findTalker: theGCurrentTalkerNumber))
		)
	)
)
