import React from 'react';
import './Header.css';

const Header: React.FC = () => {
  return (
    <header className="header">
      <div className="header__logo">GlitchIt</div>
      <div className="header__icons">
        <span className="icon search">🔍</span>
        <span className="icon heart">❤️</span>
        <span className="icon profile">👤</span>
      </div>
    </header>
  );
};

export default Header;
