;;; Sierra Script 1.0 - (do not remove this comment)
(script# 994)
(include sci.sh)
(use Main)
(use ScrollableInventory)
(use Print)
(use Polygon)
(use SaveRestoreDialog)
(use User)
(use System)


(procedure (localproc_01aa param1 &tmp temp0 [temp1 40] [temp41 40] temp81 [temp82 40] [temp122 10] [temp132 5])
	(= temp81 (Memory 1 150))
	(= temp0 1)
	(DeviceInfo 0 gSaveDir @temp1)
	(DeviceInfo 1 @temp41)
	(if
		(and
			(DeviceInfo 3 @temp41)
			(or
				(DeviceInfo 2 @temp1 @temp41)
				(not (DeviceInfo 6 (gGame name?)))
			)
		)
		(Message 0 994 6 0 0 1 @temp82)
		(Message 0 994 7 0 0 1 @temp122)
		(Message 0 994 8 0 0 1 @temp132)
		(Format
			temp81
			@temp82
			(if param1 @temp122 else @temp132)
			@temp1
		)
		(Load 135 gFont)
		(DeviceInfo 4)
		(Message 0 994 2 0 0 1 @temp82)
		(Message 0 994 4 0 0 1 @temp122)
		(Message 0 994 5 0 0 1 @temp132)
		(= temp0
			(if param1
				(Print
					font: 0
					addText: temp81
					addButton: 1 @temp82 0 40
					addButton: 0 @temp122 30 40
					addButton: 2 @temp132
					init:
				)
			else
				(Print
					font: 0
					addText: temp81
					addButton: 1 @temp82 0 40
					init:
				)
			)
		)
		(if (== temp0 2) (= temp0 (GetDirectory gSaveDir)))
	)
	(Memory 3 temp81)
	(return temp0)
)

(class Sounds of EventHandler
	(properties
		elements 0
		size 0
	)
	
	(method (pause param1)
		(self eachElementDo: 96 mayPause (if argc param1 else 1))
	)
)

(class Cue of Obj
	(properties
		cuee 0
		cuer 0
		register 0
	)
	
	(method (doit)
		(gCuees delete: self)
		(if (gCuees isEmpty:) (gCuees dispose:) (= gCuees 0))
		(cuee cue: register cuer)
		(self dispose:)
	)
)

(class Game of Obj
	(properties
		script 0
		printLang 1
		_detailLevel 3
		panelObj 0
		panelSelector 0
		handsOffCode 0
		handsOnCode 0
	)
	
	(method (init)
		(= gCast cast)
		(gCast add:)
		(= gFeatures features)
		(gFeatures add:)
		(= gSounds Sounds)
		(gSounds add:)
		(= gRegions regions)
		(gRegions add:)
		(= gAddToPics addToPics)
		(gAddToPics add:)
		(= gTimers timers)
		(gTimers add:)
		(= gTheDoits theDoits)
		(gTheDoits add:)
		(= gOldMH mouseDownHandler)
		(gOldMH add:)
		(= gOldKH keyDownHandler)
		(gOldKH add:)
		(= gOldDH directionHandler)
		(gOldDH add:)
		(= gWalkHandler walkHandler)
		(gWalkHandler add:)
		(= gFastCast 0)
		(= gSaveDir (GetSaveDir))
		(InventoryBase init:)
		(if (not gUser) (= gUser User))
		(gUser init:)
	)
	
	(method (doit &tmp newEvent thePanelObj thePanelSelector)
		(if panelObj
			(= thePanelObj panelObj)
			(= thePanelSelector panelSelector)
			(= panelObj (= panelSelector 0))
			(Eval thePanelObj thePanelSelector)
		)
		(= gGameTime (+ gTickOffset (GetTime)))
		(if gFastCast
			(while gFastCast
				(gFastCast eachElementDo: 57)
				(= newEvent (Event new:))
				(if (and (newEvent type?) gFastCast)
					(gFastCast firstTrue: 133 newEvent)
				)
				(newEvent dispose:)
				(= gGameTime (+ gTickOffset (GetTime)))
				(gSounds eachElementDo: 180)
			)
		)
		(if gPrints
			(gPrints eachElementDo: 57)
			(if (not gDialog)
				(= newEvent (Event new:))
				(if (and (newEvent type?) gPrints)
					(gPrints firstTrue: 133 newEvent)
				)
				(newEvent dispose:)
				(= gGameTime (+ gTickOffset (GetTime)))
				(return)
			)
		)
		(gSounds eachElementDo: 180)
		(gTimers eachElementDo: 57)
		(if (and gDialog (gDialog check:)) (gDialog dispose:))
		(Animate (gCast elements?) 1)
		(if gDoMotionCue
			(= gDoMotionCue 0)
			(gCast eachElementDo: 253)
		)
		(if gCuees (gCuees eachElementDo: 57))
		(if script (script doit:))
		(gRegions eachElementDo: 57)
		(if gFastCast (return))
		(if (== gNewRoomNumber gRoomNumber) (gUser doit:))
		(gTheDoits doit:)
		(if (!= gNewRoomNumber gRoomNumber)
			(self newRoom: gNewRoomNumber)
		)
		(gTimers eachElementDo: 81)
		(GameIsRestarting 0)
	)
	
	(method (play)
		(= gGame self)
		(= gSaveDir (GetSaveDir))
		(self setCursor: gWaitCursor 1 init:)
		(self setCursor: gNormalCursor 1)
		(while (not gQuitGame)
			(self doit:)
		)
	)
	
	(method (replay &tmp temp0)
		(if gPEvent (gPEvent dispose:))
		(if gDialog (gDialog dispose:))
		(gCast eachElementDo: 96 RU)
		(gGame setCursor: gWaitCursor 1)
		(= temp0
			(if (not (IsOneOf (gRoom style?) -1 11 12 13 14))
				(gRoom style?)
			else
				100
			)
		)
		(DrawPic (gRoom curPic?) temp0 1)
		(if (!= gPicNumber -1) (DrawPic gPicNumber 100 0))
		(gAddToPics doit:)
		(cond 
			(
				(and
					(not (gUser canControl:))
					(not (gUser canInput:))
				)
				(gGame setCursor: gWaitCursor)
			)
			((and gIconBar (gIconBar curIcon?)) (gGame setCursor: ((gIconBar curIcon?) cursor?)))
			(else (gGame setCursor: gNormalCursor))
		)
		(DoSound 2)
		(gSounds pause: 0)
		(= gTickOffset (- gGameTime (GetTime)))
		(while (not gQuitGame)
			(self doit:)
		)
	)
	
	(method (newRoom theGRoomNumber &tmp [temp0 5] temp5)
		(DebugPrint {Switching to room %d} theGRoomNumber)
		(gAddToPics eachElementDo: 111 eachElementDo: 81 release:)
		(gFeatures eachElementDo: 96 fDC release:)
		(gCast eachElementDo: 111 eachElementDo: 81)
		(gTimers eachElementDo: 81)
		(gRegions eachElementDo: 96 DisposeNonKeptRegion release:)
		(gTheDoits release:)
		(Animate 0)
		(= gPreviousRoomNumber gRoomNumber)
		(= gRoomNumber theGRoomNumber)
		(= gNewRoomNumber theGRoomNumber)
		(FlushResources theGRoomNumber)
		(self startRoom: gRoomNumber)
		(while ((= temp5 (Event new: 3)) type?)
			(temp5 dispose:)
		)
		(temp5 dispose:)
	)
	
	(method (startRoom param1)
		(if gDebugOnNextRoom (SetDebug))
		(= gRoom (ScriptID param1))
		(gRegions addToFront: gRoom)
		(gRoom init:)
	)
	
	(method (restart)
		(if gDialog (gDialog dispose:))
		(RestartGame)
	)
	
	(method (restore &tmp [temp0 20] temp20 temp21 [temp22 100] [temp122 5] [temp127 100])
		(if (not (ValidPath gSaveDir))
			(Message 0 994 9 0 0 1 @temp22)
			(Format @temp127 @temp22 gSaveDir)
			(Print font: 0 addText: @temp127 init:)
			(GetDirectory gSaveDir)
		)
		(Load 135 gSmallFont)
		(ScriptID 990)
		(= temp21 (self setCursor: gNormalCursor))
		(gSounds pause: 1)
		(if (localproc_01aa 1)
			(if gDialog (gDialog dispose:))
			(= temp20 (Restore doit: &rest))
			(if (!= temp20 -1)
				(self setCursor: gWaitCursor 1)
				(if (CheckSaveGame name temp20 gVersion)
					(RestoreGame name temp20 gVersion)
				else
					(Message 0 994 3 0 0 1 @temp22)
					(Message 0 994 2 0 0 1 @temp122)
					(Print
						font: 0
						addText: @temp22
						addButton: 1 @temp122 0 40
						init:
					)
					(self setCursor: temp21 (HaveMouse))
				)
			)
			(localproc_01aa 0)
		)
		(gSounds pause: 0)
	)
	
	(method (save &tmp [temp0 20] temp20 temp21 [temp22 100] [temp122 5] [temp127 100])
		(if (not (ValidPath gSaveDir))
			(Message 0 994 9 0 0 1 @temp22)
			(Format @temp127 @temp22 gSaveDir)
			(Print font: 0 addText: @temp127 init:)
			(GetDirectory gSaveDir)
		)
		(Load 135 gSmallFont)
		(ScriptID 990)
		(= temp21 (self setCursor: gNormalCursor))
		(gSounds pause: 1)
		(if (localproc_01aa 1)
			(if gDialog (gDialog dispose:))
			(= temp20 (Save doit: @temp0))
			(if (!= temp20 -1)
				(= temp21 (self setCursor: gWaitCursor 1))
				(if (not (SaveGame name temp20 @temp0 gVersion))
					(Message 0 994 1 0 0 1 @temp22)
					(Message 0 994 2 0 0 1 @temp122)
					(Print
						font: 0
						addText: @temp22
						addButton: 1 @temp122 0 40
						init:
					)
				)
				(self setCursor: temp21 (HaveMouse))
			)
			(localproc_01aa 0)
		)
		(gSounds pause: 0)
	)
	
	(method (handleEvent param1)
		(cond 
			((param1 claimed?) 1)
			((and script (script handleEvent: param1)) 1)
			((& (param1 type?) $4000) (self pragmaFail:))
		)
		(param1 claimed?)
	)
	
	(method (showMem &tmp [temp0 100])
		(Format
			@temp0
			{Free Heap: %u Bytes\nLargest ptr: %u Bytes\nFreeHunk: %u KBytes\nLargest hunk: %u Bytes}
			(MemoryInfo 1)
			(MemoryInfo 0)
			(>> (MemoryInfo 3) $0006)
			(MemoryInfo 2)
		)
		(Print addText: @temp0 init:)
	)
	
	(method (setCursor theGCursorNumber_2 param2 param3 param4 param5 param6 &tmp theGCursorNumber)
		(= theGCursorNumber gCursorNumber)
		(if (IsObject theGCursorNumber_2)
			(= gCursorNumber theGCursorNumber_2)
			(theGCursorNumber_2 init:)
		else
			(SetCursor theGCursorNumber_2 0 0)
		)
		(if (> argc 1)
			(SetCursor param2)
			(if (> argc 2)
				(SetCursor param3 param4)
				(if (> argc 4)
					(SetCursor theGCursorNumber_2 0 0 param5 param6)
				)
			)
		)
		(return theGCursorNumber)
	)
	
	(method (notify)
	)
	
	(method (setScript param1)
		(if script (script dispose:))
		(if param1 (param1 init: self &rest))
	)
	
	(method (cue)
		(if script (script cue:))
	)
	
	(method (quitGame param1)
		(if (or (not argc) param1) (= gQuitGame 1))
	)
	
	(method (masterVolume param1)
		(if argc (DoSound 0 param1) else (DoSound 0))
	)
	
	(method (detailLevel the_detailLevel)
		(if argc
			(= _detailLevel the_detailLevel)
			(gCast eachElementDo: 304)
		)
		(return _detailLevel)
	)
	
	(method (pragmaFail)
	)
	
	(method (handsOff)
		(if handsOffCode
			(handsOffCode doit: &rest)
		else
			(User canControl: 0 canInput: 0)
			(if (IsObject gEgo) (gEgo setMotion: 0))
		)
	)
	
	(method (handsOn)
		(if handsOnCode
			(handsOnCode doit: &rest)
		else
			(User canControl: 1 canInput: 1)
		)
	)
)

(class Rgn of Obj
	(properties
		script 0
		number 0
		modNum -1
		noun 0
		_case 0
		timer 0
		keep 0
		initialized 0
	)
	
	(method (init)
		(if (not initialized)
			(= initialized 1)
			(if (not (gRegions contains: self))
				(gRegions addToEnd: self)
			)
			(super init:)
		)
	)
	
	(method (doit)
		(if script (script doit:))
	)
	
	(method (dispose)
		(gRegions delete: self)
		(if (IsObject script) (script dispose:))
		(if (IsObject timer) (timer dispose: delete:))
		(gSounds eachElementDo: 181 self)
		(DisposeScript number)
	)
	
	(method (handleEvent param1)
		(cond 
			((param1 claimed?) 1)
			((& (param1 type?) $0040) 0)
			(
				(not
					(if
					(and script (or (script handleEvent: param1) 1))
						(param1 claimed?)
					)
				)
				(param1 claimed: (self doVerb: (param1 message?)))
			)
		)
		(param1 claimed?)
	)
	
	(method (doVerb param1 &tmp temp0)
		(if (== modNum -1) (= modNum gRoomNumber))
		(return
			(if (Message 0 modNum noun param1 _case 1)
				(gMessager say: noun param1 _case 0 0 modNum)
				1
			else
				0
			)
		)
	)
	
	(method (setScript param1)
		(if (IsObject script) (script dispose:))
		(if param1 (param1 init: self &rest))
	)
	
	(method (cue)
		(if script (script cue:))
	)
	
	(method (newRoom)
	)
	
	(method (notify)
	)
)

(class Rm of Rgn
	(properties
		script 0
		number 0
		modNum -1
		noun 0
		_case 0
		timer 0
		keep 0
		initialized 0
		picture 0
		style -1
		horizon 0
		controls 0
		north 0
		east 0
		south 0
		west 0
		curPic 0
		picAngle 0
		vanishingX 160
		vanishingY 0
		obstacles 0
		inset 0
	)
	
	(method (init)
		(= number gRoomNumber)
		(= gPicAngle picAngle)
		(if picture (self drawPic: picture))
		(self
			reflectPosn: (gUser alterEgo?) ((gUser alterEgo?) edgeHit?)
		)
		((gUser alterEgo?) edgeHit: 0)
	)
	
	(method (doit &tmp temp0)
		(if script (script doit:))
		(= temp0
			(self edgeToRoom: ((gUser alterEgo?) edgeHit?))
		)
		(if temp0
			(self
				newRoom: (= temp0
					(self edgeToRoom: ((gUser alterEgo?) edgeHit?))
				)
			)
		)
	)
	
	(method (dispose)
		(if obstacles (obstacles dispose:))
		(super dispose:)
	)
	
	(method (handleEvent param1)
		(if (and inset (inset handleEvent: param1))
		else
			(super handleEvent: param1)
		)
		(param1 claimed?)
	)
	
	(method (newRoom theGNewRoomNumber)
		(gRegions
			delete: self
			eachElementDo: 385 theGNewRoomNumber
			addToFront: self
		)
		(= gNewRoomNumber theGNewRoomNumber)
		(super newRoom: theGNewRoomNumber)
	)
	
	(method (setRegions param1 &tmp temp0 temp1 temp2)
		(= temp0 0)
		(while (< temp0 argc)
			(= temp1 [param1 temp0])
			(= temp2 (ScriptID temp1))
			(temp2 number: temp1)
			(gRegions add: temp2)
			(if (not (temp2 initialized?)) (temp2 init:))
			(++ temp0)
		)
	)
	
	(method (drawPic theCurPic param2)
		(if gAddToPics (gAddToPics eachElementDo: 111 release:))
		(= curPic theCurPic)
		(= gPicNumber -1)
		(DrawPic
			theCurPic
			(cond 
				((== argc 2) param2)
				((!= style -1) style)
				(else 100)
			)
			1
		)
	)
	
	(method (overlay theGPicNumber param2)
		(= gPicNumber theGPicNumber)
		(DrawPic
			theGPicNumber
			(cond 
				((== argc 2) param2)
				((!= style -1) style)
				(else 100)
			)
			0
		)
	)
	
	(method (addObstacle param1)
		(if (not (IsObject obstacles))
			(= obstacles (List new:))
		)
		(obstacles add: param1 &rest)
	)
	
	(method (reflectPosn param1 param2)
		(switch param2
			(1 (param1 y: 188))
			(4
				(param1 x: (- 319 (param1 xStep?)))
			)
			(3
				(param1 y: (+ horizon (param1 yStep?)))
			)
			(2 (param1 x: 1))
		)
	)
	
	(method (edgeToRoom param1)
		(switch param1
			(1 north)
			(2 east)
			(3 south)
			(4 west)
		)
	)
	
	(method (roomToEdge param1)
		(switch param1
			(north 1)
			(south 3)
			(east 2)
			(west 4)
		)
	)
	
	(method (setInset param1 param2 param3)
		(if inset (inset dispose:))
		(if (and argc param1)
			(param1
				init:
					(if (>= argc 2) param2 else 0)
					self
					(if (>= argc 3) param3 else 0)
			)
		)
	)
)

(instance cast of EventHandler
	(properties)
)

(instance features of EventHandler
	(properties)
)

(instance theDoits of EventHandler
	(properties)
)

(instance mayPause of Code
	(properties)
	
	(method (doit param1 param2)
		(if (not (& (param1 flags?) $0001))
			(param1 pause: param2)
		)
	)
)

(instance regions of EventHandler
	(properties)
)

(instance addToPics of EventHandler
	(properties)
	
	(method (doit)
		(self eachElementDo: 96 aTOC)
		(AddToPic elements)
	)
)

(instance timers of Set
	(properties)
)

(instance mouseDownHandler of EventHandler
	(properties)
)

(instance keyDownHandler of EventHandler
	(properties)
)

(instance directionHandler of EventHandler
	(properties)
)

(instance walkHandler of EventHandler
	(properties)
)

(instance aTOC of Code
	(properties)
	
	(method (doit param1 &tmp temp0 temp1)
		(if (not (& (param1 signal?) $4000))
			(= temp0
				(+ (gEgo xStep?) (/ (CelWide (gEgo view?) 2 0) 2))
			)
			(= temp1 (* (gEgo yStep?) 2))
			(gRoom
				addObstacle:
					((Polygon new:)
						init:
							(- (param1 brLeft?) temp0)
							(- (CoordPri 1 (CoordPri (param1 y?))) temp1)
							(+ (param1 brRight?) temp0)
							(- (CoordPri 1 (CoordPri (param1 y?))) temp1)
							(+ (param1 brRight?) temp0)
							(+ (param1 y?) temp1)
							(- (param1 brLeft?) temp0)
							(+ (param1 y?) temp1)
						yourself:
					)
			)
		)
	)
)

(instance RU of Code
	(properties)
	
	(method (doit param1 &tmp temp0)
		(if (param1 underBits?)
			(= temp0 (param1 signal?))
			(= temp0 (| temp0 $0001))
			(= temp0 (& temp0 $fffb))
			(param1 underBits: 0 signal: temp0)
		)
	)
)

(instance DisposeNonKeptRegion of Code
	(properties)
	
	(method (doit param1)
		(if (not (param1 keep?)) (param1 dispose:))
	)
)

(instance fDC of Code
	(properties)
	
	(method (doit param1)
		(if (param1 respondsTo: 81)
			(param1
				signal: (& (param1 signal?) $ffdf)
				dispose:
				delete:
			)
		else
			(param1 dispose:)
		)
	)
)
