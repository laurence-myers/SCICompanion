;;; Sierra Script 1.0 - (do not remove this comment)
(script# 990)
(include sci.sh)
(use Main)
(use Controls)
(use Print)
(use DialogControls)
(use File)

(public
	GetDirectory 0
)

(local
	local0
	local1
	local2
	local3
	local4
	[local5 15]
	[local20 15]
	[local35 15]
	[local50 15]
	[theText 25]
)
(procedure (GetDirectory param1 &tmp temp0 [temp1 33] [temp34 100] [temp134 50])
	(repeat
		(if
			(not
				(= temp0
					(Print
						font: 0
						addText: 1 0 0 1 0 0 990
						addEdit: (StrCpy @temp1 param1) 29 0 20 param1
						addButton: 1 27 0 0 1 0 34 990
						addButton: 0 38 0 0 1 50 34 990
						init:
					)
				)
			)
			(return 0)
		)
		(if (not (StrLen @temp1)) (GetCWD @temp1))
		(if (ValidPath @temp1)
			(StrCpy param1 @temp1)
			(return 1)
		)
		(Message 0 990 29 0 0 1 @temp134)
		(Format @temp34 @temp134 @temp1)
		(Print font: 0 addText: @temp34 init:)
	)
)

(procedure (localproc_01eb)
	(return
		(cond 
			((== self Restore) 0)
			((localproc_020a) 1)
			(local2 2)
			(else 3)
		)
	)
)

(procedure (localproc_020a)
	(if (< local2 20) (CheckFreeSpace gSaveDir))
)

(procedure (localproc_0218)
	(Print font: 0 addText: 3 0 0 1 0 0 990 init:)
)

(class SRDialog of Dialog
	(properties
		elements 0
		size 0
		text 0
		font 0
		window 0
		theItem 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		time 0
		caller 0
		seconds 0
		lastSeconds 0
		eatTheMice 0
		lastTicks 0
	)
	
	(method (init param1 param2 param3 &tmp [temp0 250])
		(= window gWindow)
		(= nsBottom 0)
		(= local2 (GetSaveFiles (gGame name?) param2 param3))
		(if (== local2 -1) (return 0))
		(= local4 (localproc_01eb))
		(if (== local4 1)
			(editI
				text: (StrCpy param1 param2)
				font: gSmallFont
				setSize:
				moveTo: 4 4
			)
			(self add: editI setSize:)
		)
		(selectorI
			text: param2
			font: gSmallFont
			setSize:
			moveTo: 4 (+ nsBottom 4)
			state: 2
		)
		(switch local4
			(0
				(Message 0 990 26 0 0 1 @local5)
			)
			(1
				(Message 0 990 28 0 0 1 @local5)
			)
			(else 
				(Message 0 990 25 0 0 1 @local5)
			)
		)
		(= local1 (+ (selectorI nsRight?) 4))
		(okI
			text: @local5
			setSize:
			moveTo: local1 (selectorI nsTop?)
			state:
				(if
				(or (and (== local4 0) (not local2)) (== local4 3))
					0
				else
					3
				)
		)
		(Message 0 990 24 0 0 1 @local20)
		(deleteI
			text: @local20
			setSize:
			moveTo: local1 (+ (okI nsBottom?) 4)
			state: (if (not local2) 0 else 3)
		)
		(Message 0 990 23 0 0 1 @local35)
		(changeDirI
			text: @local35
			setSize:
			moveTo: local1 (+ (deleteI nsBottom?) 4)
			state: (& (changeDirI state?) (~ $0008))
		)
		(Message 0 990 22 0 0 1 @local50)
		(cancelI
			text: @local50
			setSize:
			moveTo: local1 (+ (changeDirI nsBottom?) 4)
			state: (& (cancelI state?) (~ $0008))
		)
		(self
			add: selectorI okI deleteI changeDirI cancelI
			setSize:
		)
		(switch local4
			(0
				(Message 0 990 10 0 0 1 @temp0)
			)
			(1
				(Message 0 990 11 0 0 1 @temp0)
			)
			(else 
				(Message 0 990 30 0 0 1 @temp0)
			)
		)
		(Instance_990_10
			text: @temp0
			setSize: (- (- nsRight nsLeft) 8)
			moveTo: 4 4
		)
		(= local1 (+ (Instance_990_10 nsBottom?) 4))
		(self eachElementDo: 187 0 local1)
		(self add: Instance_990_10 setSize: center: open: 4 -1)
		(return 1)
	)
	
	(method (doit param1 &tmp newFile temp1 temp2 [temp3 361] [temp364 21] [temp385 140])
		(if (and (== self Restore) argc param1)
			(if
				(==
					(= newFile
						(FileIO 0 (Format @temp385 {%ssg.dir} (gGame name?)))
					)
					-1
				)
				(return)
			)
			(FileIO 1 newFile)
		)
		(if (not (self init: param1 @temp3 @temp364))
			(return -1)
		)
		(repeat
			(= local0
				(switch local4
					(0
						(if local2 okI else changeDirI)
					)
					(1 editI)
					(2 okI)
					(else  changeDirI)
				)
			)
			(= local1 (super doit: local0))
			(= temp2
				(*
					(= local3 (selectorI indexOf: (selectorI cursor?)))
					18
				)
			)
			(if (== local1 changeDirI)
				(self dispose:)
				(if
					(and
						(GetDirectory gSaveDir)
						(==
							(= local2 (GetSaveFiles (gGame name?) @temp3 @temp364))
							-1
						)
					)
					(= temp1 -1)
					(break)
				)
				(self init: param1 @temp3 @temp364)
				(continue)
			)
			(if (and (== local4 2) (== local1 okI))
				(self dispose:)
				(if
				(GetReplaceName doit: (StrCpy param1 @[temp3 temp2]))
					(= temp1 [temp364 local3])
					(break)
				)
				(self init: param1 @temp3 @temp364)
				(continue)
			)
			(if
				(and
					(== local4 1)
					(or (== local1 okI) (== local1 editI))
				)
				(if (== (StrLen param1) 0)
					(self dispose:)
					(localproc_0218)
					(self init: param1 @temp3 @temp364)
					(continue)
				)
				(= temp1 -1)
				(= local1 0)
				(while (< local1 local2)
					(breakif
						(not (= temp1 (StrCmp param1 @[temp3 (* local1 18)])))
					)
					(++ local1)
				)
				(if (not temp1) (= temp1 [temp364 local1]) (break))
				(if (== local2 20) (= temp1 [temp364 local3]) (break))
				(= temp1 0)
				(repeat
					(= local1 0)
					(while (< local1 local2)
						(breakif (== temp1 [temp364 local1]))
						(++ local1)
					)
					(if (== local1 local2) (break))
					(++ temp1)
				)
				(break)
			else
				(if (== local1 deleteI)
					(self dispose:)
					(if
						(not
							(Print
								addText: 12 0 0 1 0 0 990
								addButton: 0 31 0 0 1 0 35 990
								addButton: 1 32 0 0 1 50 35 990
								init:
							)
						)
						(self init: param1 @temp3 @temp364)
						(continue)
					)
					((= newFile (File new:))
						name: (DeviceInfo 7 @temp385 (gGame name?))
						open: 2
					)
					(= temp1 2570)
					(= local1 0)
					(while (< local1 local2)
						(if (!= local1 local3)
							(newFile write: @[temp364 local1] 2)
							(newFile writeString: @[temp3 (* local1 18)])
							(newFile write: @temp1 1)
						)
						(++ local1)
					)
					(= temp1 -1)
					(newFile write: @temp1 2 close: dispose:)
					(DeviceInfo 8 @temp385 (gGame name?) [temp364 local3])
					(FileIO 4 @temp385)
					(self init: param1 @temp3 @temp364)
					(continue)
				)
				(if (== local1 okI) (= temp1 [temp364 local3]) (break))
				(if (or (== local1 -1) (== local1 cancelI))
					(= temp1 -1)
					(break)
				)
				(if (== local4 1)
					(editI
						cursor: (StrLen (StrCpy param1 @[temp3 temp2]))
						draw:
					)
				)
			)
		)
		(DisposeScript 993)
		(self dispose:)
		(DisposeScript 990)
		(return temp1)
	)
	
	(method (dispose)
		(super dispose: &rest)
	)
)

(class Restore of SRDialog
	(properties
		elements 0
		size 0
		text 0
		font 0
		window 0
		theItem 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		time 0
		caller 0
		seconds 0
		lastSeconds 0
		eatTheMice 0
		lastTicks 0
	)
	
	(method (init)
		(Message 0 990 20 0 0 1 @theText)
		(= text @theText)
		(super init: &rest)
	)
)

(class Save of SRDialog
	(properties
		elements 0
		size 0
		text 0
		font 0
		window 0
		theItem 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		time 0
		caller 0
		seconds 0
		lastSeconds 0
		eatTheMice 0
		lastTicks 0
	)
	
	(method (init)
		(Message 0 990 21 0 0 1 @theText)
		(= text @theText)
		(super init: &rest)
	)
)

(instance GetReplaceName of Dialog
	(properties)
	
	(method (doit param1 &tmp temp0 [temp1 15] [temp16 15] [temp31 15] [temp46 15])
		(= window gWindow)
		(Message 0 990 33 0 0 1 @temp1)
		(text1 text: @temp1 setSize: moveTo: 4 4)
		(self add: text1 setSize:)
		(oldName
			text: param1
			font: gSmallFont
			setSize:
			moveTo: 4 nsBottom
		)
		(self add: oldName setSize:)
		(Message 0 990 34 0 0 1 @temp16)
		(text2 text: @temp16 setSize: moveTo: 4 nsBottom)
		(self add: text2 setSize:)
		(newName
			text: param1
			font: gSmallFont
			setSize:
			moveTo: 4 nsBottom
		)
		(self add: newName setSize:)
		(Message 0 990 33 0 0 1 @temp31)
		(button1 text: @temp31 nsLeft: 0 nsTop: 0 setSize:)
		(Message 0 990 38 0 0 1 @temp46)
		(button2 text: @temp46 nsLeft: 0 nsTop: 0 setSize:)
		(button2
			moveTo: (- nsRight (+ (button2 nsRight?) 4)) nsBottom
		)
		(button1
			moveTo: (- (button2 nsLeft?) (+ (button1 nsRight?) 4)) nsBottom
		)
		(self add: button1 button2 setSize: center: open: 0 -1)
		(= temp0 (super doit: newName))
		(self dispose:)
		(if (not (StrLen param1))
			(localproc_0218)
			(= temp0 0)
		)
		(return (or (== temp0 newName) (== temp0 button1)))
	)
)

(instance selectorI of DSelector
	(properties
		x 36
		y 8
	)
)

(instance editI of DEdit
	(properties
		max 35
	)
)

(instance okI of DButton
	(properties)
	
	(method (dispose)
		(super dispose: 1)
	)
)

(instance cancelI of DButton
	(properties)
	
	(method (dispose)
		(super dispose: 1)
	)
)

(instance changeDirI of DButton
	(properties)
	
	(method (dispose)
		(super dispose: 1)
	)
)

(instance deleteI of DButton
	(properties)
	
	(method (dispose)
		(super dispose: 1)
	)
)

(instance Instance_990_10 of DText
	(properties
		font 0
	)
	
	(method (dispose)
		(super dispose: 1)
	)
)

(instance text1 of DText
	(properties
		font 0
	)
	
	(method (dispose)
		(super dispose: 1)
	)
)

(instance text2 of DText
	(properties
		font 0
	)
	
	(method (dispose)
		(super dispose: 1)
	)
)

(instance oldName of DText
	(properties)
	
	(method (dispose)
		(super dispose: 1)
	)
)

(instance newName of DEdit
	(properties
		max 35
	)
)

(instance button1 of DButton
	(properties)
	
	(method (dispose)
		(super dispose: 1)
	)
)

(instance button2 of DButton
	(properties)
	
	(method (dispose)
		(super dispose: 1)
	)
)
