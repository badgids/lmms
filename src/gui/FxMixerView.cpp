/*
 * FxMixerView.cpp - effect-mixer-view for LMMS
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - http://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include <QtGlobal>
#include <QDebug>

#include <QButtonGroup>
#include <QInputDialog>
#include <QLayout>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QScrollArea>
#include <QStyle>
#include <QKeyEvent>

#include "FxMixerView.h"
#include "Knob.h"
#include "Engine.h"
#include "embed.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "gui_templates.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include "BBTrackContainer.h"

FxMixerView::FxMixerView() :
	QWidget(),
	ModelView( NULL, this ),
	SerializingObjectHook()
{
	FxMixer * m = Engine::fxMixer();
	m->setHook( this );

	//QPalette pal = palette();
	//pal.setColor( QPalette::Background, QColor( 72, 76, 88 ) );
	//setPalette( pal );

	setAutoFillBackground( true );
	setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Fixed );

	setWindowTitle( tr( "FX-Mixer" ) );
	setWindowIcon( embed::getIconPixmap( "fx_mixer" ) );

	// main-layout
	QHBoxLayout * ml = new QHBoxLayout;

	// Channel area
	m_channelAreaWidget = new QWidget;
	chLayout = new QHBoxLayout( m_channelAreaWidget );
	chLayout->setSizeConstraint( QLayout::SetMinimumSize );
	chLayout->setSpacing( 0 );
	chLayout->setMargin( 0 );
	m_channelAreaWidget->setLayout(chLayout);

	// create rack layout before creating the first channel
	m_racksWidget = new QWidget;
	m_racksLayout = new QStackedLayout( m_racksWidget );
	m_racksLayout->setContentsMargins( 0, 0, 0, 0 );
	m_racksWidget->setLayout( m_racksLayout );

	// add master channel
	m_fxChannelViews.resize( m->numChannels() );
	m_fxChannelViews[0] = new FxChannelView( this, this, 0 );

	m_racksLayout->addWidget( m_fxChannelViews[0]->m_rackView );

	FxChannelView * masterView = m_fxChannelViews[0];
	ml->addWidget( masterView->m_fxLine, 0, Qt::AlignTop );

	QSize fxLineSize = masterView->m_fxLine->size();

	// add mixer channels
	for( int i = 1; i < m_fxChannelViews.size(); ++i )
	{
		m_fxChannelViews[i] = new FxChannelView(m_channelAreaWidget, this, i);
		chLayout->addWidget( m_fxChannelViews[i]->m_fxLine );
	}

	// add the scrolling section to the main layout
	// class solely for scroll area to pass key presses down
	class ChannelArea : public QScrollArea
	{
		public:
			ChannelArea( QWidget * parent, FxMixerView * mv ) :
				QScrollArea( parent ), m_mv( mv ) {}
			~ChannelArea() {}
			virtual void keyPressEvent( QKeyEvent * e )
			{
				m_mv->keyPressEvent( e );
			}
		private:
			FxMixerView * m_mv;
	};
	channelArea = new ChannelArea( this, this );
	channelArea->setWidget( m_channelAreaWidget );
	channelArea->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	channelArea->setFrameStyle( QFrame::NoFrame );
	channelArea->setMinimumWidth( fxLineSize.width() * 6 );
	channelArea->setFixedHeight( fxLineSize.height() +
			style()->pixelMetric( QStyle::PM_ScrollBarExtent ) );
	ml->addWidget( channelArea, 1, Qt::AlignTop );

	// show the add new effect channel button
	QPushButton * newChannelBtn = new QPushButton( embed::getIconPixmap( "new_channel" ), QString::null, this );
	newChannelBtn->setObjectName( "newChannelBtn" );
	newChannelBtn->setFixedSize( fxLineSize );
	connect( newChannelBtn, SIGNAL( clicked() ), this, SLOT( addNewChannel() ) );
	ml->addWidget( newChannelBtn, 0, Qt::AlignTop );


	// add the stacked layout for the effect racks of fx channels 
	ml->addWidget( m_racksWidget, 0, Qt::AlignTop | Qt::AlignRight );
	
	setCurrentFxLine( m_fxChannelViews[0]->m_fxLine );

	setLayout( ml );
	updateGeometry();

	// timer for updating faders
	connect( gui->mainWindow(), SIGNAL( periodicUpdate() ),
					this, SLOT( updateFaders() ) );


	// add ourself to workspace
	QMdiSubWindow * subWin =
		gui->mainWindow()->workspace()->addSubWindow( this );
	Qt::WindowFlags flags = subWin->windowFlags();
	flags &= ~Qt::WindowMaximizeButtonHint;
	subWin->setWindowFlags( flags );
	layout()->setSizeConstraint( QLayout::SetMinimumSize );
	subWin->layout()->setSizeConstraint( QLayout::SetMinAndMaxSize );

	parentWidget()->setAttribute( Qt::WA_DeleteOnClose, false );
	parentWidget()->move( 5, 310 );

	// we want to receive dataChanged-signals in order to update
	setModel( m );
}

FxMixerView::~FxMixerView()
{
	for (int i=0; i<m_fxChannelViews.size(); i++)
	{
		delete m_fxChannelViews.at(i);
	}
}



int FxMixerView::addNewChannel()
{
	// add new fx mixer channel and redraw the form.
	FxMixer * mix = Engine::fxMixer();

	int newChannelIndex = mix->createChannel();
	m_fxChannelViews.push_back(new FxChannelView(m_channelAreaWidget, this,
												 newChannelIndex));
	chLayout->addWidget( m_fxChannelViews[newChannelIndex]->m_fxLine );
	m_racksLayout->addWidget( m_fxChannelViews[newChannelIndex]->m_rackView );

	updateFxLine(newChannelIndex);

	updateMaxChannelSelector();

	return newChannelIndex;
}


void FxMixerView::refreshDisplay()
{
	// delete all views and re-add them
	for( int i = 1; i<m_fxChannelViews.size(); ++i )
	{
		chLayout->removeWidget(m_fxChannelViews[i]->m_fxLine);
		m_racksLayout->removeWidget( m_fxChannelViews[i]->m_rackView );
		delete m_fxChannelViews[i]->m_fader;
		delete m_fxChannelViews[i]->m_muteBtn;
		delete m_fxChannelViews[i]->m_soloBtn;
		delete m_fxChannelViews[i]->m_fxLine;
		delete m_fxChannelViews[i]->m_rackView;
		delete m_fxChannelViews[i];
	}
	m_channelAreaWidget->adjustSize();

	// re-add the views
	m_fxChannelViews.resize(Engine::fxMixer()->numChannels());
	for( int i = 1; i < m_fxChannelViews.size(); ++i )
	{
		m_fxChannelViews[i] = new FxChannelView(m_channelAreaWidget, this, i);
		chLayout->addWidget(m_fxChannelViews[i]->m_fxLine);
		m_racksLayout->addWidget( m_fxChannelViews[i]->m_rackView );
	}
	
	// set selected fx line to 0
	setCurrentFxLine( 0 );
	
	// update all fx lines
	for( int i = 0; i < m_fxChannelViews.size(); ++i )
	{
		updateFxLine( i );
	}

	updateMaxChannelSelector();
}


// update the and max. channel number for every instrument
void FxMixerView::updateMaxChannelSelector()
{
	QVector<Track *> songTrackList = Engine::getSong()->tracks();
	QVector<Track *> bbTrackList = Engine::getBBTrackContainer()->tracks();

	QVector<Track *> trackLists[] = {songTrackList, bbTrackList};
	for(int tl=0; tl<2; ++tl)
	{
		QVector<Track *> trackList = trackLists[tl];
		for(int i=0; i<trackList.size(); ++i)
		{
			if( trackList[i]->type() == Track::InstrumentTrack )
			{
				InstrumentTrack * inst = (InstrumentTrack *) trackList[i];
				inst->effectChannelModel()->setRange(0,
					m_fxChannelViews.size()-1,1);
			}
		}
	}
}


void FxMixerView::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	MainWindow::saveWidgetState( this, _this );
}




void FxMixerView::loadSettings( const QDomElement & _this )
{
	MainWindow::restoreWidgetState( this, _this );
}


FxMixerView::FxChannelView::FxChannelView(QWidget * _parent, FxMixerView * _mv,
										  int _chIndex )
{
	m_fxLine = new FxLine(_parent, _mv, _chIndex);

	FxMixer * m = Engine::fxMixer();
	m_fader = new Fader( &m->effectChannel(_chIndex)->m_volumeModel,
					tr( "FX Fader %1" ).arg( _chIndex ), m_fxLine );
	m_fader->move( 16-m_fader->width()/2,
					m_fxLine->height()-
					m_fader->height()-5 );

	m_muteBtn = new PixmapButton( m_fxLine, tr( "Mute" ) );
	m_muteBtn->setModel( &m->effectChannel(_chIndex)->m_muteModel );
	m_muteBtn->setActiveGraphic(
				embed::getIconPixmap( "led_off" ) );
	m_muteBtn->setInactiveGraphic(
				embed::getIconPixmap( "led_green" ) );
	m_muteBtn->setCheckable( true );
	m_muteBtn->move( 9,  m_fader->y()-11);
	ToolTip::add( m_muteBtn, tr( "Mute this FX channel" ) );

	m_soloBtn = new PixmapButton( m_fxLine, tr( "Solo" ) );
	m_soloBtn->setModel( &m->effectChannel(_chIndex)->m_soloModel );
	m_soloBtn->setActiveGraphic(
				embed::getIconPixmap( "led_red" ) );
	m_soloBtn->setInactiveGraphic(
				embed::getIconPixmap( "led_off" ) );
	m_soloBtn->setCheckable( true );
	m_soloBtn->move( 9,  m_fader->y()-21);
	connect(&m->effectChannel(_chIndex)->m_soloModel, SIGNAL( dataChanged() ),
			_mv, SLOT ( toggledSolo() ) );
	ToolTip::add( m_soloBtn, tr( "Solo FX channel" ) );
	
	// Create EffectRack for the channel
	m_rackView = new EffectRackView( &m->effectChannel(_chIndex)->m_fxChain, _mv->m_racksWidget );
	m_rackView->setFixedSize( 245, FxLine::FxLineHeight );
}


void FxMixerView::toggledSolo()
{
	Engine::fxMixer()->toggledSolo();
}



void FxMixerView::setCurrentFxLine( FxLine * _line )
{
	// select
	m_currentFxLine = _line;
	m_racksLayout->setCurrentWidget( m_fxChannelViews[ _line->channelIndex() ]->m_rackView );

	// set up send knob
	for(int i = 0; i < m_fxChannelViews.size(); ++i)
	{
		updateFxLine(i);
	}
}


void FxMixerView::updateFxLine(int index)
{
	FxMixer * mix = Engine::fxMixer();

	// does current channel send to this channel?
	int selIndex = m_currentFxLine->channelIndex();
	FxLine * thisLine = m_fxChannelViews[index]->m_fxLine;
	FloatModel * sendModel = mix->channelSendModel(selIndex, index);
	if( sendModel == NULL )
	{
		// does not send, hide send knob
		thisLine->m_sendKnob->setVisible(false);
	}
	else
	{
		// it does send, show knob and connect
		thisLine->m_sendKnob->setVisible(true);
		thisLine->m_sendKnob->setModel(sendModel);
	}

	// disable the send button if it would cause an infinite loop
	thisLine->m_sendBtn->setVisible(! mix->isInfiniteLoop(selIndex, index));
	thisLine->m_sendBtn->updateLightStatus();
	thisLine->update();
}


void FxMixerView::deleteChannel(int index)
{
	// can't delete master
	if( index == 0 ) return;

	// remember selected line
	int selLine = m_currentFxLine->channelIndex();

	// delete the real channel
	Engine::fxMixer()->deleteChannel(index);

	// delete the view
	chLayout->removeWidget(m_fxChannelViews[index]->m_fxLine);
	m_racksLayout->removeWidget( m_fxChannelViews[index]->m_rackView );
	delete m_fxChannelViews[index]->m_fader;
	delete m_fxChannelViews[index]->m_muteBtn;
	delete m_fxChannelViews[index]->m_soloBtn;
	delete m_fxChannelViews[index]->m_fxLine;
	delete m_fxChannelViews[index]->m_rackView;
	delete m_fxChannelViews[index];
	m_channelAreaWidget->adjustSize();

	// make sure every channel knows what index it is
	for(int i=0; i<m_fxChannelViews.size(); ++i)
	{
		if( i > index )
		{
			m_fxChannelViews[i]->m_fxLine->setChannelIndex(i-1);
		}
	}
	m_fxChannelViews.remove(index);

	// select the next channel
	if( selLine >= m_fxChannelViews.size() )
	{
		selLine = m_fxChannelViews.size()-1;
	}
	setCurrentFxLine(selLine);

	updateMaxChannelSelector();
}



void FxMixerView::deleteUnusedChannels()
{
	TrackContainer::TrackList tracks;
	tracks += Engine::getSong()->tracks();
	tracks += Engine::getBBTrackContainer()->tracks();

	// go through all FX Channels
	for(int i = m_fxChannelViews.size()-1; i > 0; --i)
	{
		// check if an instrument references to the current channel
		bool empty=true;
		foreach( Track* t, tracks )
		{
			if( t->type() == Track::InstrumentTrack )
			{
				InstrumentTrack* inst = dynamic_cast<InstrumentTrack *>( t );
				if( i == inst->effectChannelModel()->value(0) )
				{
					empty=false;
					break;
				}
			}
		}
		FxChannel * ch = Engine::fxMixer()->effectChannel( i );
		// delete channel if no references found
		if( empty && ch->m_receives.isEmpty() )
		{
			deleteChannel( i );
		}
	}
}



void FxMixerView::moveChannelLeft(int index)
{
	// can't move master or first channel left or last channel right
	if( index <= 1 || index >= m_fxChannelViews.size() ) return;

	int selIndex = m_currentFxLine->channelIndex();

	FxMixer * mix = Engine::fxMixer();
	mix->moveChannelLeft(index);

	// refresh the two mixer views
	for( int i = index-1; i <= index; ++i )
	{
		// delete the mixer view
		int replaceIndex = chLayout->indexOf(m_fxChannelViews[i]->m_fxLine);

		chLayout->removeWidget(m_fxChannelViews[i]->m_fxLine);
		m_racksLayout->removeWidget( m_fxChannelViews[i]->m_rackView );
		delete m_fxChannelViews[i]->m_fader;
		delete m_fxChannelViews[i]->m_muteBtn;
		delete m_fxChannelViews[i]->m_soloBtn;
		delete m_fxChannelViews[i]->m_fxLine;
		delete m_fxChannelViews[i];

		// add it again
		m_fxChannelViews[i] = new FxChannelView( m_channelAreaWidget, this, i );
		chLayout->insertWidget( replaceIndex, m_fxChannelViews[i]->m_fxLine );
		m_racksLayout->insertWidget( replaceIndex, m_fxChannelViews[i]->m_rackView );
	}

	// keep selected channel
	if( selIndex == index )
	{
		selIndex = index-1;
	}
	else if( selIndex == index - 1 )
	{
		selIndex = index;
	}
	setCurrentFxLine(selIndex);
}



void FxMixerView::moveChannelRight(int index)
{
	moveChannelLeft(index+1);
}



void FxMixerView::keyPressEvent(QKeyEvent * e)
{
	switch(e->key())
	{
		case Qt::Key_Delete:
			deleteChannel(m_currentFxLine->channelIndex());
			break;
		case Qt::Key_Left:
			if( e->modifiers() & Qt::AltModifier )
			{
				moveChannelLeft( m_currentFxLine->channelIndex() );
			}
			else
			{
				// select channel to the left
				setCurrentFxLine( m_currentFxLine->channelIndex()-1 );
			}
			break;
		case Qt::Key_Right:
			if( e->modifiers() & Qt::AltModifier )
			{
				moveChannelRight( m_currentFxLine->channelIndex() );
			}
			else
			{
				// select channel to the right
				setCurrentFxLine( m_currentFxLine->channelIndex()+1 );
			}
			break;
	}
}



void FxMixerView::closeEvent( QCloseEvent * _ce )
 {
	if( parentWidget() )
	{
		parentWidget()->hide();
	}
	else
	{
		hide();
	}
	_ce->ignore();
 }



void FxMixerView::setCurrentFxLine( int _line )
{
	if( _line >= 0 && _line < m_fxChannelViews.size() )
	{
		setCurrentFxLine( m_fxChannelViews[_line]->m_fxLine );
	}
}



void FxMixerView::clear()
{
	Engine::fxMixer()->clear();

	refreshDisplay();
}




void FxMixerView::updateFaders()
{
	FxMixer * m = Engine::fxMixer();

	// apply master gain
	m->m_fxChannels[0]->m_peakLeft *= Engine::mixer()->masterGain();
	m->m_fxChannels[0]->m_peakRight *= Engine::mixer()->masterGain();

	for( int i = 0; i < m_fxChannelViews.size(); ++i )
	{
		const float opl = m_fxChannelViews[i]->m_fader->getPeak_L();
		const float opr = m_fxChannelViews[i]->m_fader->getPeak_R();
		const float fall_off = 1.2;
		if( m->m_fxChannels[i]->m_peakLeft > opl )
		{
			m_fxChannelViews[i]->m_fader->setPeak_L( m->m_fxChannels[i]->m_peakLeft );
			m->m_fxChannels[i]->m_peakLeft = 0;
		}
		else
		{
			m_fxChannelViews[i]->m_fader->setPeak_L( opl/fall_off );
		}

		if( m->m_fxChannels[i]->m_peakRight > opr )
		{
			m_fxChannelViews[i]->m_fader->setPeak_R( m->m_fxChannels[i]->m_peakRight );
			m->m_fxChannels[i]->m_peakRight = 0;
		}
		else
		{
			m_fxChannelViews[i]->m_fader->setPeak_R( opr/fall_off );
		}
	}
}





