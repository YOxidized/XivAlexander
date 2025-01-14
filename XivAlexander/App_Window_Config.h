#pragma once
#include "App_Window_Base.h"

namespace App::Window {
	class Config : public Base {
		App::Config::BaseRepository* const m_pRepository;
		
		HWND m_hScintilla = nullptr;
		SciFnDirect m_direct = nullptr;
		sptr_t m_directPtr = 0;
		std::string m_originalConfig;

		Utils::CallOnDestruction m_callbackHandle;

	public:
		explicit Config(App::Config::BaseRepository* pRepository);
		~Config() override;

		void Revert();
		bool TrySave();

		[[nodiscard]]
		App::Config::BaseRepository* GetRepository() const;
		
	protected:
		LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnLayout(double zoom, double width, double height) override;
		LRESULT OnNotify(const LPNMHDR nmhdr) override;

		void ResizeMargin();
	};
};