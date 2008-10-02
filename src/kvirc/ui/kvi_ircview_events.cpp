//===========================================================================
//
//   File : kvi_ircview_events.cpp
//   Creation date : Wed Oct 1 2008 17:18:20 by Fabio Bas
//
//   This file is part of the KVirc irc client distribution
//   Copyright (C) 2008 Fabio Bas (ctrlaltca at gmail dot com)
//
//   This program is FREE software. You can redistribute it and/or
//   modify it under the terms of the GNU General Public License
//   as published by the Free Software Foundation; either version 2
//   of the License, or (at your opinion) any later version.
//
//   This program is distributed in the HOPE that it will be USEFUL,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//   See the GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, write to the Free Software Foundation,
//   Inc. ,59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
//===========================================================================
//
// This file was originally part of kvi_ircview.cpp
//
//===========================================================================

#include "kvi_channel.h"
#include "kvi_kvs_eventtriggers.h"
#include "kvi_iconmanager.h"
#include "kvi_ircconnectiontarget.h"
#include "kvi_ircurl.h"
#include "kvi_ircview.h"
#include "kvi_ircviewprivate.h"
#include "kvi_ircviewtools.h"
#include "kvi_locale.h"
#include "kvi_mirccntrl.h"
#include "kvi_options.h"
#include "kvi_out.h"
#include "kvi_topicw.h"
#include "kvi_query.h"
#include "kvi_window.h"

#include <QClipboard>
#include <QEvent>
#include <QMouseEvent>
#include <QResource>
#include <QScrollBar>
#include <QUrl>

#define KVI_IRCVIEW_SELECT_REPAINT_INTERVAL 100

/*
	@doc: escape_sequences
	@title:
		Escape sequences and clickable links
	@type:
		generic
	@body:
		The KVIrc view widgets support clickable links.[br]
		The links can be created using special escape sequences in the text
		passed to the [cmd]echo[/cmd] command.[br]
		KVIrc uses some escape sequences in the text "echoed" internally.[br]
		The simplest way to explain it is to use an example:[br]
		[example]
			[cmd]echo[/cmd] This is a [fnc]$cr[/fnc]![!dbl][cmd]echo[/cmd] You have clicked it![fnc]$cr[/fnc]\clickable link$cr !
		[/example]
		The example above will show the following text line: "This is a clickable link".
		If you move the mouse over the words "clickable link", you will see the text highlighted.[br]
		Once you double-click one of that words, the command "[cmd]echo[/cmd] You have clicked it!" will be executed.[br]
		The format looks complex ?... it is not...just read on.[br]

		<cr>!<link_type><cr><visible text><cr>
		<cr>!<escape_command><cr><visible text><cr>

		[big]Escape format[/big]
		The whole escape sequence format is the following:[br]
		[b]<cr>!<escape_command><cr><visible text><cr>[/b][br]
		<cr> is the carriage return character. You can obtain it by using the [fnc]]$cr[/fnc] function.[br]
		<visible text> is the text that will appear as "link" when you move the mouse over it.[br]
		<escape_command> is the description of the actions to be taken when the user interacts with the link.[br]
		<escape_command> has the two following syntactic forms:[br]
		[b]<escape_command> ::= <user_defined_commands>[/b][br]
		[b]<escape_command> ::= <builtin_link_description>[/b]

		[big]User defined links[/big][br]
		The user defined links allow you to perform arbitrary commands when the user interacts with the link.[br]
		The commands are specified in the <escape_command> part by using the following syntax:[br]
		<escape_command> ::= <user_defined_commands>[br]
		<user_defined_commands> ::= <command_rule> [<user_defined_commands>][br]
		<command_rule> ::= <action_tag><command>[br]
		<action_tag> ::= "[!" <action> "]"[br]
		<action> ::= "rbt" | "mbt" | "dbl" | "txt"[br]
		<command> ::= any kvirc command (see notes below)[br]

		[big]Builtin links[/big][br]
		The builtin links have builtin actions performed when the user interact with the link.[br]
		These links are used internally in KVIrc , but you can use them too.[br]
		The <escape_command> is a single letter this time: it defines the type of the link.[br]
		Currently KVIrc uses six types of builtin links : 'n' for nickname links, 'u' for url links,
		'c' for channel links, 'h' for hostname links, 'm' for mask links and 's' for server links.[br]
		Theoretically you can also use your own link types: just use any other letter or digit (you can't use ']' and <cr>),
		but probably you will prefer a completely user defined link in that case anyway.[br]
		Once the user interacts with the link , kvirc executes the predefined events:[br]
		On right-click the event OnBuiltinLinkRightClicked is triggered: the first parameter is the link type,
		the second parameter is the <visible text> (as a single parameter!!!)[br]
		On middle-click the event OnBuiltinLinkMiddleClicked is triggered: the parameters are similar to the previous one.[br]
		In the same way you have OnBuiltinLinkDoubleClicked.[br]

		[big]A shortcut[/big]
		You may have a look at the [fnc]$fmtlink[/fnc] function: it does automatically some of the job explained
		in this document.[br]

*/

// FIXME: #warning "Finish the doc above!! Maybe some examples ?!"


//mouse events
void KviIrcView::mouseDoubleClickEvent(QMouseEvent *e)
{
	QString cmd;
	QString linkCmd;
	QString linkText;

	if(m_iMouseTimer)
	{
		killTimer(m_iMouseTimer);
		m_iMouseTimer=0;
		delete m_pLastEvent;
		m_pLastEvent = 0;
	}

	getLinkUnderMouse(e->pos().x(),e->pos().y(),0,&linkCmd,&linkText);

	if(linkCmd.isEmpty())
	{
		KVS_TRIGGER_EVENT_0(KviEvent_OnTextViewDoubleClicked,m_pKviWindow);
		return;
	}

	QString szCmd(linkCmd);
	szCmd.remove(0,1);

	KviKvsVariantList * pParams = new KviKvsVariantList();
	if(!szCmd.isEmpty()) pParams->append(szCmd);
	else pParams->append(linkText);
	pParams->append(linkText);
	pParams->append(szCmd);


	switch(linkCmd[0].unicode())
	{
		case 'n':
		{
			bool bTrigger = false;
			switch(m_pKviWindow->type())
			{
				case KVI_WINDOW_TYPE_CHANNEL:
					if(((KviChannel *)m_pKviWindow)->isOn(linkText))
					{
						KVS_TRIGGER_EVENT(KviEvent_OnChannelNickDefaultActionRequest,m_pKviWindow,pParams);
					} else bTrigger = true;
				break;
				case KVI_WINDOW_TYPE_QUERY:
					if(KviQString::equalCI(((KviQuery *)m_pKviWindow)->windowName(),linkText))
					{
						KVS_TRIGGER_EVENT(KviEvent_OnQueryNickDefaultActionRequest,m_pKviWindow,pParams);
					} else bTrigger = true;
				break;
				default:
					bTrigger = true;
				break;
			}
			if(bTrigger)
			{
				if(console())
				{
					KVS_TRIGGER_EVENT(KviEvent_OnNickLinkDefaultActionRequest,m_pKviWindow,pParams);
				}
			}
		}
		break;
		case 'm':
			if((linkCmd.length() > 2) && (m_pKviWindow->type() == KVI_WINDOW_TYPE_CHANNEL))
			{
				if(((KviChannel *)m_pKviWindow)->isMeOp())
				{
					QChar plmn = linkCmd[1];
					if((plmn.unicode() == '+') || (plmn.unicode() == '-'))
					{
						QString target(m_pKviWindow->windowName());
						target.replace("\\","\\\\");
						target.replace("\"","\\\"");
						target.replace(";","\\;");
						target.replace("$","\\$");
						target.replace("%","\\%");
						QChar flag = linkCmd[2];
						switch(flag.unicode())
						{
							case 'o':
							case 'v':
								// We can do nothing here...
							break;

							case 'b':
							case 'I':
							case 'e':
							case 'k':
								KviQString::sprintf(cmd,"mode %Q %c%c $0",&target,plmn.toLatin1(),flag.toLatin1());
							break;
							default:
								KviQString::sprintf(cmd,"mode %Q %c%c",&target,plmn.toLatin1(),flag.toLatin1());
							break;
						}
					}
				}
			}
		break;
		case 'h':
			m_pKviWindow->output(KVI_OUT_HOSTLOOKUP,__tr2qs("Looking up host %Q..."),&linkText);
			cmd = "host -a $0";
		break;
		case 'u':
			{
				QString urlText;
				if(!szCmd.isEmpty()) urlText=szCmd;
				else urlText=linkText;
				if(
					!KviQString::cmpCIN(urlText,"irc://",6) ||
					!KviQString::cmpCIN(urlText,"irc6://",7) ||
					!KviQString::cmpCIN(urlText,"ircs://",7) ||
					!KviQString::cmpCIN(urlText,"ircs6://",8)
					)
				{
					KviIrcUrl::run(urlText,KviIrcUrl::TryCurrentContext | KviIrcUrl::DoNotPartChans, console());
				} else {
					// Check for number of clicks
					if(KVI_OPTION_UINT(KviOption_uintUrlMouseClickNum) == 2) cmd = "openurl $0";
				}
			}
		break;
		case 'c':
			{
				if(console() && console()->connection())
				{
					QString szChan=linkText;
					if(szCmd.length()>0) szChan=szCmd;
					if(KviChannel * c = console()->connection()->findChannel(szChan))
					{
						// FIXME: #warning "Is this ok ?"
						c->raise();
						c->setFocus();
					} else {
						cmd = QString("join %1").arg(szChan);
					}
				}
			}
		break;
		case 's':
			cmd = "motd $0";
		break;
		default:
		{
			getLinkEscapeCommand(cmd,linkCmd,"[!dbl]");
			if(cmd.isEmpty())
			{
				KVS_TRIGGER_EVENT_0(KviEvent_OnTextViewDoubleClicked,m_pKviWindow);
			}
		}
		break;
	}
	if(!cmd.isEmpty())
	{
		KviKvsScript::run(cmd,m_pKviWindow,pParams);
	}
	delete pParams;
}

void KviIrcView::mousePressEvent(QMouseEvent *e)
{
	if(m_pKviWindow->input()) m_pKviWindow->input()->setFocus();

	if(e->button() & Qt::LeftButton)
	{
		// This is the beginning of a selection...
		// We just set the mouse to be "down" and
		// await mouseMove events...

		if(m_pToolWidget)
		{
			m_pCursorLine = getVisibleLineAt(e->pos().x(),e->pos().y());
			repaint();
		}

		m_mousePressPos   = e->pos();
		m_mouseCurrentPos = e->pos();

		m_bMouseIsDown = true;

		m_bShiftPressed = (e->modifiers() & Qt::ShiftModifier);

		calculateSelectionBounds();
	}

	if(e->button() & Qt::LeftButton)
	{
		if(m_iMouseTimer)
		{
			killTimer(m_iMouseTimer);
			m_iMouseTimer=0;
			delete m_pLastEvent;
			m_pLastEvent = 0;
		} else {
			m_iMouseTimer = startTimer(QApplication::doubleClickInterval());
			m_pLastEvent = new QMouseEvent(*e);
		}
	} else {
		mouseRealPressEvent(e);
	}
}

void KviIrcView::mouseRealPressEvent(QMouseEvent *e)
{
	QString linkCmd;
	QString linkText;
	getLinkUnderMouse(e->pos().x(),e->pos().y(),0,&linkCmd,&linkText);

	QString szCmd(linkCmd);
	szCmd.remove(0,1);

	KviKvsVariantList * pParams = new KviKvsVariantList();
	if(!szCmd.isEmpty()) pParams->append(szCmd);
	else pParams->append(linkText);
	pParams->append(linkText);
	pParams->append(szCmd);


	if(!(e->modifiers() & Qt::ControlModifier))//(e->button() & Qt::RightButton) && (
	{
		if(!linkCmd.isEmpty())
		{
			switch(linkCmd[0].unicode())
			{
				case 'n':
					{
						bool bTrigger = false;
						switch(m_pKviWindow->type())
						{
							case KVI_WINDOW_TYPE_CHANNEL:
								if(((KviChannel *)m_pKviWindow)->isOn(linkText))
								{
									if(e->button() & Qt::RightButton)
										KVS_TRIGGER_EVENT(KviEvent_OnChannelNickPopupRequest,m_pKviWindow,pParams);
									if(e->button() & Qt::LeftButton) {
										KVS_TRIGGER_EVENT(KviEvent_OnChannelNickLinkClick,m_pKviWindow,pParams);
										if(m_pKviWindow)
										{
											if(m_pKviWindow->inherits("KviChannel")) {
												KviChannel *c = (KviChannel*)m_pKviWindow;
												QString nick;
												if(pParams->firstAsString(nick))
													c->userListView()->select(nick);
											}
										}
									}
								} else bTrigger = true;
							break;
							case KVI_WINDOW_TYPE_QUERY:
								if(KviQString::equalCI(((KviQuery *)m_pKviWindow)->windowName(),linkText))
								{
									if(e->button() & Qt::RightButton)
										KVS_TRIGGER_EVENT(KviEvent_OnQueryNickPopupRequest,m_pKviWindow,pParams);
									if(e->button() & Qt::LeftButton)
										KVS_TRIGGER_EVENT(KviEvent_OnQueryNickLinkClick,m_pKviWindow,pParams);
								} else bTrigger = true;
							break;
							default:
								bTrigger = true;
						break;
						}
						if(bTrigger)
						{
							if(console())
							{
								if(e->button() & Qt::RightButton)
									KVS_TRIGGER_EVENT(KviEvent_OnNickLinkPopupRequest,m_pKviWindow,pParams);
								if(e->button() & Qt::LeftButton)
									KVS_TRIGGER_EVENT(KviEvent_OnConsoleNickLinkClick,m_pKviWindow,pParams);
							} else emit rightClicked();
						}
					}
				break;
				case 'h':
					if(e->button() & Qt::RightButton)
						KVS_TRIGGER_EVENT(KviEvent_OnHostLinkPopupRequest,m_pKviWindow,pParams);
					if(e->button() & Qt::LeftButton)
						KVS_TRIGGER_EVENT(KviEvent_OnHostLinkClick,m_pKviWindow,pParams);
				break;
				case 'u':
					if(e->button() & Qt::RightButton)
						KVS_TRIGGER_EVENT(KviEvent_OnURLLinkPopupRequest,m_pKviWindow,pParams);
					if(e->button() & Qt::LeftButton)
					{
						KVS_TRIGGER_EVENT(KviEvent_OnURLLinkClick,m_pKviWindow,pParams);

						// Check for clicks' number
						QString cmd;
						if(KVI_OPTION_UINT(KviOption_uintUrlMouseClickNum) == 1) cmd = "openurl $0";
						KviKvsScript::run(cmd,m_pKviWindow,pParams);
					}
				break;
				case 'c':
					if(e->button() & Qt::RightButton)
						KVS_TRIGGER_EVENT(KviEvent_OnChannelLinkPopupRequest,m_pKviWindow,pParams);
					if(e->button() & Qt::LeftButton)
						KVS_TRIGGER_EVENT(KviEvent_OnChannelLinkClick,m_pKviWindow,pParams);
				break;
				case 's':
					if(e->button() & Qt::RightButton)
						KVS_TRIGGER_EVENT(KviEvent_OnServerLinkPopupRequest,m_pKviWindow,pParams);
					if(e->button() & Qt::LeftButton)
						KVS_TRIGGER_EVENT(KviEvent_OnServerLinkClick,m_pKviWindow,pParams);
				break;
				default:
				{
					if(e->button() & Qt::RightButton)
					{
						QString tmp;
						getLinkEscapeCommand(tmp,linkCmd,"[!rbt]");
						if(!tmp.isEmpty())
						{
							KviKvsScript::run(tmp,m_pKviWindow,pParams);
						} else emit rightClicked();
					}
				}
				break;
			}
		} else if(e->button() & Qt::RightButton) emit rightClicked();

	} else if((e->button() & Qt::MidButton) || ((e->button() & Qt::RightButton) && (e->modifiers() & Qt::ControlModifier)))
	{
		QString tmp;
		getLinkEscapeCommand(tmp,linkCmd,QString("[!mbt]"));
		if(!tmp.isEmpty())
		{
			KviKvsScript::run(tmp,m_pKviWindow,pParams);
		} else {
			KVS_TRIGGER_EVENT_0(KviEvent_OnWindowPopupRequest,m_pKviWindow);
		}
	}
	delete pParams;
}

void KviIrcView::mouseReleaseEvent(QMouseEvent *)
{
	if(m_iSelectTimer)
	{
		killTimer(m_iSelectTimer);
		m_iSelectTimer = 0;
		QClipboard * c = QApplication::clipboard();
		if(c)
		{
			// copy to both!
			c->setText(m_szLastSelection,QClipboard::Clipboard);
			if(c->supportsSelection())
				c->setText(m_szLastSelection,QClipboard::Selection);
		}
	}

	if(m_bMouseIsDown)
	{
		m_bMouseIsDown = false;
		m_bShiftPressed = false;
		// Insert the lines blocked while selecting
		while(KviIrcViewLine * l = m_pMessagesStoppedWhileSelecting->first())
		{
			m_pMessagesStoppedWhileSelecting->removeFirst();
			appendLine(l,false);
		}
		repaint();
	}
}

// FIXME: #warning "The tooltip timeout should be small, because the view scrolls!"

void KviIrcView::mouseMoveEvent(QMouseEvent *e)
{
//	debug("Pos : %d,%d",e->pos().x(),e->pos().y());
	if(m_bMouseIsDown && (e->buttons() & Qt::LeftButton)) // m_bMouseIsDown MUST BE true...(otherwise the mouse entered the window with the button pressed ?)
	{

		if(m_iSelectTimer == 0)m_iSelectTimer = startTimer(KVI_IRCVIEW_SELECT_REPAINT_INTERVAL);

		//scroll the ircview if the user is trying to extend a selection near the ircview borders
		int curY = e->pos().y();
		if(curY < KVI_IRCVIEW_VERTICAL_BORDER)
		{
			prevLine();
		} else if(curY > (height() - KVI_IRCVIEW_VERTICAL_BORDER)) {
			nextLine();
		}
		/*if(m_iMouseTimer)
		{
			killTimer(m_iMouseTimer);
			m_iMouseTimer=0;
			mouseRealPressEvent(m_pLastEvent);
			delete m_pLastEvent;
			m_pLastEvent=0;
		}*/
	} else {
		if(m_iSelectTimer)
		{
			killTimer(m_iSelectTimer);
			m_iSelectTimer = 0;
		}

		int yPos = e->pos().y();
		int rectTop;
		int rectHeight;
		QRect rctLink;
		KviIrcViewWrappedBlock * newLinkUnderMouse = getLinkUnderMouse(e->pos().x(),yPos,&rctLink);

		rectTop = rctLink.y();
		rectHeight = rctLink.height();

		if(newLinkUnderMouse != m_pLastLinkUnderMouse)
		{
			//abortTip();
			//m_iTipTimer = startTimer(KVI_OPTION_UINT(KviOption_uintIrcViewToolTipTimeoutInMsec));
			m_pLastLinkUnderMouse = newLinkUnderMouse;
			if(m_pLastLinkUnderMouse)
			{
				setCursor(Qt::PointingHandCursor);
				if(rectTop < 0)rectTop = 0;
				if((rectTop + rectHeight) > height())rectHeight = height() - rectTop;

				if(m_iLastLinkRectHeight > -1)
				{
					// prev link
					int top = (rectTop < m_iLastLinkRectTop) ? rectTop : m_iLastLinkRectTop;
					int lastBottom = m_iLastLinkRectTop + m_iLastLinkRectHeight;
					int thisBottom = rectTop + rectHeight;
					QRect r(0,top,width(),((lastBottom > thisBottom) ? lastBottom : thisBottom) - top);
					repaint(r);
				} else {
					// no prev link
					QRect r(0,rectTop,width(),rectHeight);
					repaint(r);
				}
				m_iLastLinkRectTop = rectTop;
				m_iLastLinkRectHeight = rectHeight;
			} else {
				setCursor(Qt::ArrowCursor);
				if(m_iLastLinkRectHeight > -1)
				{
					// There was a previous bottom rect
					QRect r(0,m_iLastLinkRectTop,width(),m_iLastLinkRectHeight);
					repaint(r);
					m_iLastLinkRectTop = -1;
					m_iLastLinkRectHeight = -1;
				}
			}

		}
	}
}

void KviIrcView::leaveEvent(QEvent * )
{
	if(m_pLastLinkUnderMouse)
	{
		 m_pLastLinkUnderMouse=0;
		 update();
	}
}

void KviIrcView::timerEvent(QTimerEvent *e)
{
	m_mouseCurrentPos = mapFromGlobal(QCursor::pos());

	if(e->timerId() == m_iSelectTimer)
	{
		calculateSelectionBounds();
		repaint();
	}
	if(e->timerId() == m_iMouseTimer)
	{
		killTimer(m_iMouseTimer);
		m_iMouseTimer=0;
		mouseRealPressEvent(m_pLastEvent);
		delete m_pLastEvent;
		m_pLastEvent=0;
	}
	if(e->timerId() == m_iFlushTimer)
	{
		flushLog();
	}
}

//not exactly events, but event-related

void KviIrcView::maybeTip(const QPoint &pnt)
{
	QString linkCmd;
	QString linkText;
	QRect rctLink;
	QRect markerArea;

	// Check if the mouse is over the marker icon
	// 16(width) + 5(border) = 21
	int widgetWidth = width()-m_pScrollBar->width();
	int x = widgetWidth - 21;
	int y = KVI_IRCVIEW_VERTICAL_BORDER;

	markerArea = QRect(QPoint(x,y),QSize(16,16));
	if(checkMarkerArea(markerArea,pnt)) doMarkerToolTip(markerArea);

	// Check if the mouse is over a link
	KviIrcViewWrappedBlock * linkUnderMouse = getLinkUnderMouse(pnt.x(),pnt.y(),&rctLink,&linkCmd,&linkText);

	if((linkUnderMouse == m_pLastLinkUnderMouse) && linkUnderMouse)doLinkToolTip(rctLink,linkCmd,linkText);
	else m_pLastLinkUnderMouse = 0; //
}

void KviIrcView::doMarkerToolTip(const QRect &rct)
{
	QString tip;
	tip = "<table width=\"100%\">" \
		"<tr><td valign=\"center\"><img src=\":/marker_icon\"> <u><font color=\"blue\"><nowrap>";
	tip += __tr2qs("Scroll up to read from the last read line");
	tip += "</nowrap></font></u></td></tr><tr><td>";
	QResource::registerResource(g_pIconManager->getSmallIcon(KVI_SMALLICON_UNREADTEXT)->toImage().bits(), "/marker_icon");
	tip += "</td></tr></table>";

	if(tip.isEmpty())return;

	m_pToolTip->doTip(rct,tip);
}

void KviIrcView::doLinkToolTip(const QRect &rct,QString &linkCmd,QString &linkText)
{
	if(linkCmd.isEmpty())return;

	QString szCmd(linkCmd);
	szCmd.remove(0,1);

	QString tip;

	switch(linkCmd[0].unicode())
	{
		case 'u': // url link
		{
			tip = "<table width=\"100%\">" \
				"<tr><td valign=\"center\"><img src=\":/url_icon\"> <u><font color=\"blue\"><nowrap>";
			if(linkText.length() > 50)
			{
				tip += linkText.left(47);
				tip += "...";
			} else {
				tip += linkText;
			}
			tip+="</nowrap></font></u></td></tr><tr><td>";
			QResource::registerResource(g_pIconManager->getSmallIcon(KVI_SMALLICON_URL)->toImage().bits(), "/url_icon");

			// Check clicks' number
			if(KVI_OPTION_UINT(KviOption_uintUrlMouseClickNum) == 1)
				tip += __tr2qs("Click to open this link");
			else
				tip += __tr2qs("Double-click to open this link");
			tip += "</td></tr></table>";
		}
		break;
		case 'h': // host link
		{
			tip = "<table width=\"100%\">" \
				"<tr><td valign=\"center\"><img src=\":/host_icon\"> <u><font color=\"blue\"><nowrap>";
			if(linkText.length() > 50)
			{
				tip += linkText.left(47);
				tip += "...";
			} else {
				tip += linkText;
			}
			tip+="</nowrap></font></u></td></tr><tr><td>";
			QResource::registerResource(g_pIconManager->getSmallIcon(KVI_SMALLICON_SERVER)->toImage().bits(), "/host_icon");

			if(linkText.indexOf('*') != -1)
			{
				if(linkText.length() > 1)tip += __tr2qs("Unable to look it up hostname: Hostname appears to be masked");
				else tip += __tr2qs("Unable to look it up hostname: Unknown host");
			} else {
				tip += __tr2qs("Double-click to look up this hostname<br>Right-click to view other options");
			}
			tip += "</td></tr></table>";
		}
		break;
		case 's': // server link
		{
			// FIXME: #warning "Spit out some server info...hub ?...registered ?"

			tip = "<table width=\"100%\">" \
				"<tr><td valign=\"center\"><img src=\":/server_icon\"> <u><font color=\"blue\"><nowrap>";
			QResource::registerResource(g_pIconManager->getSmallIcon(KVI_SMALLICON_IRC)->toImage().bits(), "/server_icon");

			if(linkText.length() > 50)
			{
				tip += linkText.left(47);
				tip += "...";
			} else {
				tip += linkText;
			}
			tip+="</nowrap></font></u></td></tr><tr><td>";

			if(linkText.indexOf('*') != -1)
			{
				if(linkText.length() > 1)tip += __tr2qs("Server appears to be a network hub<br>");
				else tip += __tr2qs("Unknown server<br>"); // might happen...
			}

			tip.append(__tr2qs("Double-click to read the MOTD<br>Right-click to view other options"));
			tip += "</td></tr></table>";
		}
		break;
		case 'm': // mode link
		{
			if((linkCmd.length() > 2) && (m_pKviWindow->type() == KVI_WINDOW_TYPE_CHANNEL))
			{
				if(((KviChannel *)m_pKviWindow)->isMeOp())
				{
					QChar plmn = linkCmd[1];
					if((plmn.unicode() == '+') || (plmn.unicode() == '-'))
					{
						tip = __tr2qs("Double-click to set<br>");
						QChar flag = linkCmd[2];
						switch(flag.unicode())
						{
							case 'o':
							case 'v':
								// We can do nothing here...
								tip = "";
							break;
							case 'b':
							case 'I':
							case 'e':
							case 'k':
								KviQString::appendFormatted(tip,QString("<b>mode %Q %c%c %Q</b>"),&(m_pKviWindow->windowName()),plmn.toLatin1(),flag.toLatin1(),&linkText);
							break;
							default:
								KviQString::appendFormatted(tip,QString("<b>mode %Q %c%c</b>"),&(m_pKviWindow->windowName()),plmn.toLatin1(),flag.toLatin1());
							break;
						}
					}
				} else {
					// I'm not op...no way
					tip = __tr2qs("You're not an operator: You may not change channel modes");
				}
			}
		}
		break;
		case 'n': // nick link
		{
			if(console())
			{
				if(console()->connection())
				{
					KviIrcUserEntry * e = console()->connection()->userDataBase()->find(linkText);
					if(e)
					{
						QString buffer;
						console()->getUserTipText(linkText,e,buffer);
						tip = buffer;
					} else KviQString::sprintf(tip,__tr2qs("Nothing known about %Q"),&linkText);
				} else KviQString::sprintf(tip,__tr2qs("Nothing known about %Q (no connection)"),&linkText);
			}
		}
		break;
		case 'c': // channel link
		{
			if(console() && console()->connection())
			{
				QString szChan = linkText;
				QString buf;
				tip = "<img src=\":/chan_icon\"> ";
				QResource::registerResource(g_pIconManager->getSmallIcon(KVI_SMALLICON_CHANNEL)->toImage().bits(), "/chan_icon");

				if(szCmd.length()>0) szChan=szCmd;
				KviChannel * c = console()->connection()->findChannel(szChan);
				QString szUrl;
				if(c)
				{
					QString chanMode;
					c->getChannelModeString(chanMode);
					QString topic = KviMircCntrl::stripControlBytes(c->topicWidget()->topic());
					topic.replace("<","&lt;");
					topic.replace(">","&gt;");
					KviIrcUrl::join(szUrl,console()->connection()->target()->server());
					szUrl.append(szChan);
					KviQString::sprintf(buf,__tr2qs("<b>%Q</b> (<u><font color=\"blue\"><nowrap>"
						"%Q</nowrap></font></u>): <br><nowrap>+%Q (%u users)<hr>%Q</nowrap>"),&szChan,&szUrl,&chanMode,
						c->count(),&topic);
				} else {
					KviIrcUrl::join(szUrl,console()->connection()->target()->server());
					szUrl.append(szChan);
					KviQString::sprintf(buf,__tr2qs("<b>%Q</b> (<u><font color=\"blue\"><nowrap>"
						"%Q</nowrap></font></u>)<hr>Double-click to join %Q<br>Right click to view other options"),&szChan,&szUrl,&szChan);
				}

				tip += buf;
			}
		}
		break;
		default:
		{
			QString dbl,rbt,txt,mbt;
			getLinkEscapeCommand(dbl,linkCmd,"[!dbl]");
			getLinkEscapeCommand(rbt,linkCmd,"[!rbt]");
			getLinkEscapeCommand(txt,linkCmd,"[!txt]");
			getLinkEscapeCommand(mbt,linkCmd,"[!mbt]");

			if(!txt.isEmpty())tip = txt;
			if(tip.isEmpty() && (!dbl.isEmpty()))
			{
				if(!tip.isEmpty())tip.append("<hr>");
				KviQString::appendFormatted(tip,__tr2qs("<b>Double-click:</b><br>%Q"),&dbl);
			}
			if(tip.isEmpty() && (!mbt.isEmpty()))
			{
				if(!tip.isEmpty())tip.append("<hr>");
				KviQString::appendFormatted(tip,__tr2qs("<b>Middle-click:</b><br>%Q"),&mbt);
			}
			if(tip.isEmpty() && (!rbt.isEmpty()))
			{
				if(!tip.isEmpty())tip.append("<hr>");
				KviQString::appendFormatted(tip,__tr2qs("<b>Right-click:</b><br>%Q"),&rbt);
			}
		}
		break;
	}

	if(tip.isEmpty())return;

	m_pToolTip->doTip(rct,tip);
}

//keyboard events
void KviIrcView::keyPressEvent(QKeyEvent *e)
{
	switch(e->key())
	{
		case Qt::Key_PageUp:
			prevPage();
			e->accept();
			break;
		case Qt::Key_PageDown:
			nextPage();
			e->accept();
			break;
		default:
			e->ignore();
	}
}

//drag&drop events
void KviIrcView::dragEnterEvent(QDragEnterEvent *e)
{
	if(!m_bAcceptDrops)return;
	//e->accept(KviUriDrag::canDecode(e));
	if(e->mimeData()->hasUrls()) e->acceptProposedAction();
	emit dndEntered();
}

void KviIrcView::dropEvent(QDropEvent *e)
{
	if(!m_bAcceptDrops)return;
	//QStringList list;
	QList<QUrl> list;
	if(e->mimeData()->hasUrls())
	//if(KviUriDrag::decodeLocalFiles(e,list))
	{
		list = e->mimeData()->urls();
		if(!list.isEmpty())
		{
			QList<QUrl>::Iterator it = list.begin();
			//QStringList::ConstIterator it = list.begin(); //kewl ! :)
			for( ; it != list.end(); ++it )
			{
				QUrl url = *it;
				QString path = url.path();
				//QString tmp = *it; //wow :)
				#if !defined(COMPILE_ON_WINDOWS) && !defined(COMPILE_ON_MINGW)
					//if(tmp[0] != '/')tmp.prepend("/"); //HACK HACK HACK for Qt bug (?!?)
					if(path[0] != '/')path.prepend("/"); //HACK HACK HACK for Qt bug (?!?)
				#endif
				//emit fileDropped(tmp);
				emit fileDropped(path);
			}
		}
	}
}

//

bool KviIrcView::event(QEvent *e)
{
	if(e->type() == QEvent::User)
	{
		__range_valid(m_bPostedPaintEventPending);
		if(m_iUnprocessedPaintEventRequests)
			repaint();
		// else we just had a pointEvent that did the job
		m_bPostedPaintEventPending = false;
		return true;
	}
	return QWidget::event(e);
}

void KviIrcView::wheelEvent(QWheelEvent *e)
{
	static bool bHere = false;
	if(bHere)return;
	bHere = true; // Qt4 tends to jump into infinite recursion here
	g_pApp->sendEvent(m_pScrollBar,e);
	bHere = false;
}

